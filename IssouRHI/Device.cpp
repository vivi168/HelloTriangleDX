#include "IssouRHI.h"
#include "Utils.h"

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

  if (GPUSelection.Index != UINT32_MAX) {
    // Cannot specify both index and name.
    if (!GPUSelection.Substring.empty()) {
      return adapter;
    }

    CHECK_HR(GetDXGIFactory()->EnumAdapters1(GPUSelection.Index, &adapter));
    return adapter;
  }

  if (!GPUSelection.Substring.empty()) {
    ComPtr<IDXGIAdapter1> tmpAdapter;
    for (UINT i = 0; GetDXGIFactory()->EnumAdapters1(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
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
  GetDXGIFactory()->EnumAdapters1(0, &adapter);
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

  // GPU Upload Heap Supported
  D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
  CHECK_HR(m_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16)));
  assert(options16.GPUUploadHeapSupported);

#ifdef ENABLE_DEBUG_LAYER
  ID3D12InfoQueue* pInfoQueue = nullptr;
  m_Device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
  pInfoQueue->Release();
  debugController->Release();
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
    D3D12_COMMAND_QUEUE_DESC qDesc{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };

    ID3D12CommandQueue* commandQueue = nullptr;
    CHECK_HR(m_Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&commandQueue)));
    m_CommandQueue.Attach(commandQueue);
  }

  // Create descriptor heaps
  {
    m_SrvUavDescriptorHeap.Create(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, NUM_DESCRIPTORS_PER_HEAP,
                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
    m_RtvDescriptorHeap.Create(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, NUM_DESCRIPTORS_PER_HEAP,
                               D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
    m_DepthStencilDescriptorHeap.Create(m_Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, NUM_DESCRIPTORS_PER_HEAP,
                                        D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
  }
}

Device::~Device()
{
  m_CommandQueue.Reset();

  PrintStatsString(m_Allocator.Get());
  m_Allocator.Reset();

  if (ENABLE_CPU_ALLOCATION_CALLBACKS) {
    assert(g_CpuAllocationCount.load() == 0);
  }

  m_Device.Reset();
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
  wprintf(L"    DedicatedVideoMemory = %zu B (%s)\n", adapterDesc.DedicatedVideoMemory,
          SizeToStr(adapterDesc.DedicatedVideoMemory).c_str());
  wprintf(L"    DedicatedSystemMemory = %zu B (%s)\n", adapterDesc.DedicatedSystemMemory,
          SizeToStr(adapterDesc.DedicatedSystemMemory).c_str());
  wprintf(L"    SharedSystemMemory = %zu B (%s)\n", adapterDesc.SharedSystemMemory,
          SizeToStr(adapterDesc.SharedSystemMemory).c_str());

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
      const DXGI_MEMORY_SEGMENT_GROUP group =
          groupIndex == 0 ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
      const wchar_t* const groupName =
          groupIndex == 0 ? L"DXGI_MEMORY_SEGMENT_GROUP_LOCAL" : L"DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL";
      DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
      CHECK_HR(adapter3->QueryVideoMemoryInfo(0, group, &info));

      wprintf(L"    %s:\n", groupName);
      wprintf(L"        Budget = %llu B (%s)\n", info.Budget, SizeToStr(info.Budget).c_str());
      wprintf(L"        CurrentUsage = %llu B (%s)\n", info.CurrentUsage, SizeToStr(info.CurrentUsage).c_str());
      wprintf(L"        AvailableForReservation = %llu B (%s)\n", info.AvailableForReservation,
              SizeToStr(info.AvailableForReservation).c_str());
      wprintf(L"        CurrentReservation = %llu B (%s)\n", info.CurrentReservation,
              SizeToStr(info.CurrentReservation).c_str());
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

std::shared_ptr<Texture> Device::CreateTexture(TextureDesc& desc)
{
  auto tex = std::make_shared<Texture>(this, desc);

  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};
  if (desc.usage & TextureUsage::CopyDst) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
  } else {
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  }

  D3D12_RESOURCE_DESC textureDesc = Texture::D3D12ResourceDesc(desc);

  ID3D12Resource* resource;
  D3D12MA::Allocation* allocation;
  CHECK_HR(m_Allocator->CreateResource(&allocDesc, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, &allocation,
                                       IID_PPV_ARGS(&resource)));

  tex->Attach(resource, allocation);
  return tex;
}

}  // namespace IssouRHI
