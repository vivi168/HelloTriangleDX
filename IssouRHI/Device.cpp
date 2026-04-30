#include "IssouRHI.h"

#include <shlwapi.h>

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{
static const bool ENABLE_CPU_ALLOCATION_CALLBACKS = true;
static const bool ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT = true;
static void* const CUSTOM_ALLOCATION_PRIVATE_DATA = (void*)(uintptr_t)0xDEADC0DE;
static std::atomic<size_t> g_CpuAllocationCount{0};

static constexpr D3D_FEATURE_LEVEL FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1;

static constexpr UINT NUM_DESCRIPTORS_PER_HEAP = 16384;

static ComPtr<IDXGIAdapter1> SelectAdapter(const GPUSelection& GPUSelection)
{
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIFactory4> dxgiFactory;
  UINT dxgiFactoryFlags = 0;

#ifdef ENABLE_DEBUG_LAYER
  ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();

    // Enable additional debug layers.
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif

  CHECK_HR(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

  if (GPUSelection.Index != UINT32_MAX) {
    // Cannot specify both index and name.
    if (!GPUSelection.Substring.empty()) {
      return adapter;
    }

    CHECK_HR(dxgiFactory->EnumAdapters1(GPUSelection.Index, &adapter));
    return adapter;
  }

  if (!GPUSelection.Substring.empty()) {
    ComPtr<IDXGIAdapter1> tmpAdapter;
    for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC1 desc;
      tmpAdapter->GetDesc1(&desc);
      if (StrStrI(desc.Description, GPUSelection.Substring.c_str())) {
        // Second matching adapter found - error.
        if (adapter) {
          adapter.Reset();
          return adapter;
        }
        // First matching adapter found.
        adapter = std::move(tmpAdapter);
      } else {
        tmpAdapter.Reset();
      }
    }
    // Found or not, return it.
    return adapter;
  }

  // Select first one.
  dxgiFactory->EnumAdapters1(0, &adapter);
  return adapter;
}

static void* CustomAllocate(size_t Size, size_t Alignment, void* pPrivateData)
{
  assert(pPrivateData == CUSTOM_ALLOCATION_PRIVATE_DATA);

  void* memory = _aligned_malloc(Size, Alignment);

  if (ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT) {
    wprintf(L"Allocate Size=%llu Alignment=%llu -> %p\n", Size, Alignment, memory);
  }

  g_CpuAllocationCount++;

  return memory;
}

static void CustomFree(void* pMemory, void* pPrivateData)
{
  assert(pPrivateData == CUSTOM_ALLOCATION_PRIVATE_DATA);

  if (pMemory) {
    g_CpuAllocationCount--;

    if (ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT) {
      wprintf(L"Free %p\n", pMemory);
    }

    _aligned_free(pMemory);
  }
}

static void PrintStatsString(D3D12MA::Allocator* allocator)
{
  WCHAR* statsString = NULL;
  allocator->BuildStatsString(&statsString, TRUE);
  wprintf(L"%s\n", statsString);
  allocator->FreeStatsString(statsString);
}

Device::Device(const GPUSelection& gpuSelection)
{
  m_Adapter = SelectAdapter(gpuSelection);

#ifdef ENABLE_DEBUG_LAYER
  ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
  }
#endif

  ID3D12Device5* device = nullptr;
  CHECK_HR(D3D12CreateDevice(m_Adapter.Get(), FEATURE_LEVEL, IID_PPV_ARGS(&device)));
  m_Device.Attach(device);

  // Ray tracing capabilities
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  CHECK_HR(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));
  assert(options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);

  // Mesh shading
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
  CHECK_HR(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)));
  assert(options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);

  // Enhanced Barriers
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {};
  CHECK_HR(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12)));
  assert(options12.EnhancedBarriersSupported);

  // GPU Upload Heap Supported
  D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
  CHECK_HR(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16)));
  assert(options16.GPUUploadHeapSupported);

#ifdef ENABLE_DEBUG_LAYER
  ComPtr<ID3D12InfoQueue> infoQueue;
  if (SUCCEEDED(m_Device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
  }
#endif

  // Create Memory allocator
  {
    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
    desc.pDevice = m_Device.Get();
    desc.pAdapter = m_Adapter.Get();

    if (ENABLE_CPU_ALLOCATION_CALLBACKS) {
      m_AllocationCallbacks.pAllocate = &CustomAllocate;
      m_AllocationCallbacks.pFree = &CustomFree;
      m_AllocationCallbacks.pPrivateData = CUSTOM_ALLOCATION_PRIVATE_DATA;
      desc.pAllocationCallbacks = &m_AllocationCallbacks;
    }

    CHECK_HR(D3D12MA::CreateAllocator(&desc, &m_Allocator));

    PrintAdapterInformation();
  }

  // Create Command Queue
  {
    m_Queue = std::make_unique<Queue>(this);
    m_Queue->Create();
  }

  // Misc
  {
    m_Queue->GetNativeQueue()->GetTimestampFrequency(&m_TimestampFrequencyHz);
  }

  // Create descriptor heaps
  {
    m_CbvSrvUavDescriptorHeap.Create(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, NUM_DESCRIPTORS_PER_HEAP, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    m_RtvDescriptorHeap.Create(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NUM_DESCRIPTORS_PER_HEAP, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
    m_DsvDescriptorHeap.Create(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, NUM_DESCRIPTORS_PER_HEAP, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
  }

  // Create root signature
  {
    // Root parameters
    constexpr UINT RootParameterCount = 2;
    constexpr UINT ConstantCount = 32;
    constexpr UINT IndirectArgumentConstantCount = 1;

    CD3DX12_ROOT_PARAMETER rootParameters[RootParameterCount]{};
    rootParameters[0].InitAsConstants(ConstantCount, 0);
    // DX12 only CHEAT: inject indirect command buffer payload in the second root parameter. accessible via cbuffer (b1)
    rootParameters[1].InitAsConstants(IndirectArgumentConstantCount, 1);

    // Static sampler
    // TODO: this has nothing to do here.
    // FIXME: get read of static sampler and pass them from app side via sampler descriptor heap
    constexpr UINT StaticSamplerCount = 2;
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[StaticSamplerCount];
    staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
    staticSamplers[1].Init(1, D3D12_FILTER_ANISOTROPIC);

    // Root Signature
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(RootParameterCount, rootParameters, StaticSamplerCount, staticSamplers, flags);

    ID3DBlob* signatureBlobPtr;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signatureBlobPtr, nullptr));

    ID3D12RootSignature* rootSignature = nullptr;
    CHECK_HR(device->CreateRootSignature(0, signatureBlobPtr->GetBufferPointer(), signatureBlobPtr->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    m_RootSignature.Attach(rootSignature);

    // Command Signatures
    {
      constexpr UINT NumArgumentDescs = 2;
      D3D12_INDIRECT_ARGUMENT_DESC argumentDescs[NumArgumentDescs]{};
      argumentDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
      argumentDescs[0].Constant = {
          .RootParameterIndex = 1,
          .DestOffsetIn32BitValues = 0,
          .Num32BitValuesToSet = IndirectArgumentConstantCount,
      };
      D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc{};
      commandSignatureDesc.NumArgumentDescs = NumArgumentDescs;
      commandSignatureDesc.pArgumentDescs = argumentDescs;

      constexpr size_t ConstantSize = sizeof(UINT) * IndirectArgumentConstantCount;

      // Dispatch
      {
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        commandSignatureDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS) + ConstantSize;

        CHECK_HR(m_Device->CreateCommandSignature(&commandSignatureDesc, RootSignature(), IID_PPV_ARGS(&m_DispatchSignature)));
      }

      // Draw
      {
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS) + ConstantSize;

        CHECK_HR(m_Device->CreateCommandSignature(&commandSignatureDesc, RootSignature(), IID_PPV_ARGS(&m_DrawCommandSignature)));
      }

      // DrawIndexed
      {
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        commandSignatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + ConstantSize;

        CHECK_HR(m_Device->CreateCommandSignature(&commandSignatureDesc, RootSignature(), IID_PPV_ARGS(&m_DrawIndirectCommandSignature)));
      }

      // DispatchMesh
      {
        argumentDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

        commandSignatureDesc.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS) + ConstantSize;

        CHECK_HR(m_Device->CreateCommandSignature(&commandSignatureDesc, RootSignature(), IID_PPV_ARGS(&m_DispatchMeshCommandSignature)));
      }
    }
  }
}

Device::~Device()
{
  PrintStatsString(m_Allocator.Get());
  m_Allocator.Reset();

  if (ENABLE_CPU_ALLOCATION_CALLBACKS) {
    assert(g_CpuAllocationCount.load() == 0);
  }
}

static const wchar_t* VendorIDToStr(uint32_t vendorID)
{
  constexpr uint32_t VENDOR_ID_AMD = 0x1002;
  constexpr uint32_t VENDOR_ID_NVIDIA = 0x10DE;
  constexpr uint32_t VENDOR_ID_INTEL = 0x8086;

  switch (vendorID) {
    case 0x10001:
      return L"VIV";
    case 0x10002:
      return L"VSI";
    case 0x10003:
      return L"KAZAN";
    case 0x10004:
      return L"CODEPLAY";
    case 0x10005:
      return L"MESA";
    case 0x10006:
      return L"POCL";
    case VENDOR_ID_AMD:
      return L"AMD";
    case VENDOR_ID_NVIDIA:
      return L"NVIDIA";
    case VENDOR_ID_INTEL:
      return L"Intel";
    case 0x1010:
      return L"ImgTec";
    case 0x13B5:
      return L"ARM";
    case 0x5143:
      return L"Qualcomm";
  }
  return L"";
}

static std::wstring SizeToStr(size_t size)
{
  if (size == 0) return L"0";

  wchar_t result[32];
  double size2 = (double)size;

  if (size2 >= 1024.0 * 1024.0 * 1024.0 * 1024.0) {
    swprintf_s(result, L"%.2f TB", size2 / (1024.0 * 1024.0 * 1024.0 * 1024.0));
  } else if (size2 >= 1024.0 * 1024.0 * 1024.0) {
    swprintf_s(result, L"%.2f GB", size2 / (1024.0 * 1024.0 * 1024.0));
  } else if (size2 >= 1024.0 * 1024.0) {
    swprintf_s(result, L"%.2f MB", size2 / (1024.0 * 1024.0));
  } else if (size2 >= 1024.0) {
    swprintf_s(result, L"%.2f KB", size2 / 1024.0);
  } else {
    swprintf_s(result, L"%llu B", size);
  }
  return result;
}

void Device::PrintAdapterInformation()
{
  if (!m_Adapter) return;

  DXGI_ADAPTER_DESC1 adapterDesc{};
  CHECK_HR(m_Adapter->GetDesc1(&adapterDesc));

  wprintf(L"DXGI_ADAPTER_DESC1:\n");
  wprintf(L"    Description = %s\n", adapterDesc.Description);
  wprintf(L"    VendorId = 0x%X (%s)\n", adapterDesc.VendorId, VendorIDToStr(adapterDesc.VendorId));
  wprintf(L"    DeviceId = 0x%X\n", adapterDesc.DeviceId);
  wprintf(L"    SubSysId = 0x%X\n", adapterDesc.SubSysId);
  wprintf(L"    Revision = 0x%X\n", adapterDesc.Revision);
  wprintf(L"    DedicatedVideoMemory = %zu B (%s)\n", adapterDesc.DedicatedVideoMemory, SizeToStr(adapterDesc.DedicatedVideoMemory).c_str());
  wprintf(L"    DedicatedSystemMemory = %zu B (%s)\n", adapterDesc.DedicatedSystemMemory, SizeToStr(adapterDesc.DedicatedSystemMemory).c_str());
  wprintf(L"    SharedSystemMemory = %zu B (%s)\n", adapterDesc.SharedSystemMemory, SizeToStr(adapterDesc.SharedSystemMemory).c_str());

  const D3D12_FEATURE_DATA_D3D12_OPTIONS& options = m_Allocator->GetD3D12Options();
  wprintf(L"D3D12_FEATURE_DATA_D3D12_OPTIONS:\n");
  wprintf(L"    StandardSwizzle64KBSupported = %u\n", options.StandardSwizzle64KBSupported ? 1 : 0);
  wprintf(L"    CrossAdapterRowMajorTextureSupported = %u\n", options.CrossAdapterRowMajorTextureSupported ? 1 : 0);

  switch (options.ResourceHeapTier) {
    case D3D12_RESOURCE_HEAP_TIER_1:
      wprintf(L"    ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1\n");
      break;
    case D3D12_RESOURCE_HEAP_TIER_2:
      wprintf(L"    ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2\n");
      break;
    default:
      assert(0);
  }

  ComPtr<IDXGIAdapter3> adapter3;

  if (SUCCEEDED(m_Adapter->QueryInterface(IID_PPV_ARGS(&adapter3)))) {
    wprintf(L"DXGI_QUERY_VIDEO_MEMORY_INFO:\n");

    for (UINT groupIndex = 0; groupIndex < 2; ++groupIndex) {
      const DXGI_MEMORY_SEGMENT_GROUP group = groupIndex == 0 ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
      const wchar_t* const groupName = groupIndex == 0 ? L"DXGI_MEMORY_SEGMENT_GROUP_LOCAL" : L"DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL";
      DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
      CHECK_HR(adapter3->QueryVideoMemoryInfo(0, group, &info));

      wprintf(L"    %s:\n", groupName);
      wprintf(L"        Budget = %llu B (%s)\n", info.Budget, SizeToStr(info.Budget).c_str());
      wprintf(L"        CurrentUsage = %llu B (%s)\n", info.CurrentUsage, SizeToStr(info.CurrentUsage).c_str());
      wprintf(L"        AvailableForReservation = %llu B (%s)\n", info.AvailableForReservation, SizeToStr(info.AvailableForReservation).c_str());
      wprintf(L"        CurrentReservation = %llu B (%s)\n", info.CurrentReservation, SizeToStr(info.CurrentReservation).c_str());
    }
  }

  assert(m_Device);
  D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1{};

  if (SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture1, sizeof architecture1))) {
    wprintf(L"D3D12_FEATURE_DATA_ARCHITECTURE1:\n");
    wprintf(L"    UMA: %u\n", architecture1.UMA ? 1 : 0);
    wprintf(L"    CacheCoherentUMA: %u\n", architecture1.CacheCoherentUMA ? 1 : 0);
    wprintf(L"    IsolatedMMU: %u\n", architecture1.IsolatedMMU ? 1 : 0);
  }
}

std::shared_ptr<QuerySet> Device::CreateQuerySet(const QuerySetDesc& desc)
{
  auto qs = std::make_shared<QuerySet>(this, desc);
  qs->Create();

  return qs;
}

std::shared_ptr<Texture> Device::CreateTexture(const TextureDesc& desc)
{
  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};
  if (desc.usage & TextureUsage::CopyDst) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
  } else {
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  }

  D3D12_RESOURCE_DESC1 textureDesc = Texture::D3D12ResourceDesc(desc);

  D3D12_CLEAR_VALUE zero{};
  zero.Format = textureDesc.Format;

  // TODO: how to allow for another clear value / any value for .clearValue of ColorAttachment?
  D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr;
  if (textureDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
    pOptimizedClearValue = &zero;
  }

  D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_COMMON;
  if (textureDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    zero.DepthStencil.Depth = 1.0f;
    zero.DepthStencil.Stencil = 0;
    initialLayout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
  }

  ID3D12Resource* resource;
  D3D12MA::Allocation* allocation;
  CHECK_HR(m_Allocator->CreateResource3(&allocDesc, &textureDesc, initialLayout, pOptimizedClearValue, 0, nullptr, &allocation, IID_PPV_ARGS(&resource)));

  resource->SetName(StringToWstring(desc.label).c_str());

  auto tex = std::make_shared<Texture>(this, desc);
  // TODO: use the same Create() pattern as Queue and QuerySet?
  tex->Attach(resource, allocation);

  return tex;
}

std::shared_ptr<Buffer> Device::CreateBuffer(const BufferDesc& desc)
{
  assert(!((desc.usage & BufferUsage::MapRead) && (desc.usage & BufferUsage::MapWrite)));

  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};
  if (desc.usage & BufferUsage::MapRead) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
  } else if (desc.usage & BufferUsage::MapWrite) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
  } else {
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  }

  auto bufferDesc = CD3DX12_RESOURCE_DESC1::Buffer(desc.size);
  if (desc.usage & BufferUsage::Storage) {
    bufferDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }
  if (desc.usage & BufferUsage::RayTracingAccelerationStructure) {
    bufferDesc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
  }

  ID3D12Resource* resource;
  D3D12MA::Allocation* allocation;
  CHECK_HR(m_Allocator->CreateResource3(&allocDesc, &bufferDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr, &allocation, IID_PPV_ARGS(&resource)));
  resource->SetName(StringToWstring(desc.label).c_str());

  auto buf = std::make_shared<Buffer>(this, desc);
  // TODO: use the same Create() pattern as Queue and QuerySet?
  buf->Attach(resource, allocation);

  return buf;
}

std::shared_ptr<AccelerationStructure> Device::CreateAccelerationStructure(const AccelerationStructureDesc& desc)
{
  auto as = std::make_shared<AccelerationStructure>(this);
  as->Create(desc);

  return as;
}

std::shared_ptr<ComputePipeline> Device::CreateComputePipeline(const ComputePipelineDesc& desc)
{
  auto computePipeline = std::make_shared<ComputePipeline>(this);
  computePipeline->Create(desc);

  return computePipeline;
}

std::shared_ptr<RenderPipeline> Device::CreateRenderPipeline(const GraphicPipelineDesc& desc)
{
  auto renderPipeline = std::make_shared<RenderPipeline>(this);
  renderPipeline->Create(desc);

  return renderPipeline;
}

std::shared_ptr<MeshPipeline> Device::CreateMeshPipeline(const GraphicPipelineDesc& desc)
{
  auto meshPipeline = std::make_shared<MeshPipeline>(this);
  meshPipeline->Create(desc);

  return meshPipeline;
}

std::shared_ptr<RayTracingPipeline> Device::CreateRayTracingPipelinePipeline(const RayTracingPipelineDesc& desc)
{
  auto rayTracingPipeline = std::make_shared<RayTracingPipeline>(this);
  rayTracingPipeline->Create(desc);

  return rayTracingPipeline;
}

std::shared_ptr<ShaderTable> Device::CreateShaderTable(const ShaderTableDesc& desc)
{
  auto shaderTable = std::make_shared<ShaderTable>(this);
  shaderTable->Create(desc);

  return shaderTable;
}

DescriptorAllocation Device::AllocCbvSrvUavDescriptor()
{
  return m_CbvSrvUavDescriptorHeap.Alloc();
}

DescriptorAllocation Device::AllocRtvDescriptor()
{
  return m_RtvDescriptorHeap.Alloc();
}

DescriptorAllocation Device::AllocDsvDescriptor()
{
  return m_DsvDescriptorHeap.Alloc();
}

void Device::FreeSrvUavDescriptor(DescriptorAllocation alloc)
{
  m_CbvSrvUavDescriptorHeap.Free(alloc);
}

void Device::FreeRtvDescriptor(DescriptorAllocation alloc)
{
  m_RtvDescriptorHeap.Free(alloc);
}

void Device::FreeDsvDescriptor(DescriptorAllocation alloc)
{
  m_DsvDescriptorHeap.Free(alloc);
}
}  // namespace IssouRHI
