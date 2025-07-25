#include "stdafx.h"

#include "DirectXTex.h"

#include "Renderer.h"
#include "RendererHelper.h"

#include "Win32Application.h"

#include "Camera.h"
#include "Mesh.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Renderer
{
// ========== Constants

#ifdef _DEBUG
#define ENABLE_DEBUG_LAYER true
#endif

static const bool ENABLE_CPU_ALLOCATION_CALLBACKS = true;
static const bool ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT = true;
static void* const CUSTOM_ALLOCATION_PRIVATE_DATA = (void*)(uintptr_t)0xDEADC0DE;

static constexpr size_t FRAME_BUFFER_COUNT = 3;
static constexpr size_t MESH_INSTANCE_COUNT = 10'000;
static constexpr UINT PRESENT_SYNC_INTERVAL = 0;
static constexpr UINT NUM_DESCRIPTORS_PER_HEAP = 16384;

static constexpr DXGI_FORMAT VISIBILITY_BUFFER_FORMAT = DXGI_FORMAT_R32_UINT;
static constexpr DXGI_FORMAT GBUFFER_WORLD_POSITION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
static constexpr DXGI_FORMAT GBUFFER_WORLD_NORMAL_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM;
static constexpr DXGI_FORMAT GBUFFER_BASE_COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT GBUFFER_METALLIC_ROUGHNESS_FORMAT = DXGI_FORMAT_R8G8_UNORM;
static constexpr DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
static constexpr D3D_FEATURE_LEVEL FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1;

// ========== Enums

enum class PSO { BasicMS, SkinningCS, InstanceCullingCS, FillGBufferCS, FinalComposeVS };

namespace RootParameter
{
enum Slots : size_t { PerDrawConstants = 0, FrameConstants, BuffersDescriptorIndices, Count };
}

namespace SkinningCSRootParameter
{
enum Slots : size_t { BuffersOffsets = 0, BuffersDescriptorIndices, Count };
}

namespace Timestamp
{
enum Timestamps : size_t {
  SkinBegin = 0,
  SkinEnd,
  CullBegin,
  CullEnd,
  DrawBegin,
  DrawEnd,
  FillGBufferBegin,
  FillGBufferEnd,
  TotalBegin,
  TotalEnd,
  Count
};
}

// ========== Structs

struct DrawMeshCommand {
  struct {
    UINT instanceIndex;
  } constants;
  D3D12_DISPATCH_MESH_ARGUMENTS args;
};

static constexpr UINT DRAW_MESH_CMDS_SIZE = MESH_INSTANCE_COUNT * sizeof(DrawMeshCommand);
static constexpr UINT DRAW_MESH_CMDS_COUNTER_OFFSET = AlignForUavCounter(DRAW_MESH_CMDS_SIZE);

struct SkinnedMeshInstance;

struct MeshInstance {
  MeshInstanceData data;

  UINT instanceBufferOffset;
  std::shared_ptr<SkinnedMeshInstance> skinnedMeshInstance = nullptr; // not null if skinned mesh
  std::shared_ptr<Mesh3D> mesh = nullptr;
};

// only used for compute shader skinning pass
struct SkinnedMeshInstance {
  struct {
    UINT basePositionsBuffer;
    UINT baseNormalsBuffer;
    UINT baseTangentsBuffer;
    UINT blendWeightsAndIndicesBuffer;
    UINT boneMatricesBuffer;
  } offsets;

  UINT numVertices;
  UINT numBoneMatrices;
  std::shared_ptr<MeshInstance> meshInstance = nullptr;  // should never be null

  size_t BoneMatricesBufferSize() const { return sizeof(XMFLOAT4X4) * numBoneMatrices; }

  SkinningPerDispatchConstants BuffersOffsets() const
  {
    assert(meshInstance);
    return {
        .firstPosition = offsets.basePositionsBuffer,
        .firstSkinnedPosition = meshInstance->data.firstPosition,
        .firstNormal = offsets.baseNormalsBuffer,
        .firstSkinnedNormal = meshInstance->data.firstNormal,
        .firstTangent = meshInstance->data.firstTangent,
        .firstSkinnedTangent = offsets.baseTangentsBuffer,
        .firstBWI = offsets.blendWeightsAndIndicesBuffer,
        .firstBoneMatrix = offsets.boneMatricesBuffer,
        .numVertices = numVertices,
    };
  }
};

struct Scene {
  struct SceneNode {
    Model3D* model;
    std::vector<std::shared_ptr<MeshInstance>> meshInstances;
    std::vector<std::shared_ptr<SkinnedMeshInstance>> skinnedMeshInstances; // should be per Skin, not per Model...
  };

  std::vector<SceneNode> nodes;

  std::unordered_map<std::wstring, std::vector<std::shared_ptr<MeshInstance>>> meshInstanceMap;
  UINT numMeshInstances = 0;
  std::vector<std::shared_ptr<SkinnedMeshInstance>> skinnedMeshInstances;
  UINT numBoneMatrices = 0;

  Camera* camera;
};

struct DescriptorHeapListAllocator {
  void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
  {
    assert(m_Heap == nullptr && m_FreeIndices.empty());
    m_Heap = heap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
    m_HeapType = desc.Type;
    m_HeapStartCpu = m_Heap->GetCPUDescriptorHandleForHeapStart();

    if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
      m_HeapStartGpu = m_Heap->GetGPUDescriptorHandleForHeapStart();
    }

    m_HeapHandleIncrement =
        device->GetDescriptorHandleIncrementSize(m_HeapType);

    for (UINT i = 0; i < desc.NumDescriptors; i++) m_FreeIndices.push_back(i);
  }

  void Destroy()
  {
    m_Heap = nullptr;
    m_FreeIndices.clear();
  }

  UINT Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescHandle,
             D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescHandle)
  {
    assert(m_FreeIndices.size() > 0);

    UINT idx = m_FreeIndices.front();
    m_FreeIndices.pop_front();

    outCpuDescHandle->ptr = m_HeapStartCpu.ptr + (idx * m_HeapHandleIncrement);
    outGpuDescHandle->ptr = m_HeapStartGpu.ptr + (idx * m_HeapHandleIncrement);

    return idx;
  }

  UINT Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescHandle)
  {
    assert(m_FreeIndices.size() > 0);

    UINT idx = m_FreeIndices.front();
    m_FreeIndices.pop_front();

    outCpuDescHandle->ptr = m_HeapStartCpu.ptr + (idx * m_HeapHandleIncrement);

    return idx;
  }

  void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle)
  {
    UINT cpuIdx = static_cast<UINT>((cpuDescHandle.ptr - m_HeapStartCpu.ptr) /
                                    m_HeapHandleIncrement);
    UINT gpuIdx = static_cast<UINT>((gpuDescHandle.ptr - m_HeapStartGpu.ptr) /
                                    m_HeapHandleIncrement);

    assert(cpuIdx == gpuIdx);
    m_FreeIndices.push_front(cpuIdx);
  }

  void Free(UINT index) { m_FreeIndices.push_front(index); }

private:
  ID3D12DescriptorHeap* m_Heap = nullptr;
  D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
  D3D12_CPU_DESCRIPTOR_HANDLE m_HeapStartCpu;
  D3D12_GPU_DESCRIPTOR_HANDLE m_HeapStartGpu;
  UINT m_HeapHandleIncrement;
  std::deque<UINT> m_FreeIndices;
};

struct HeapDescriptor {
  void Alloc(DescriptorHeapListAllocator& allocator)
  {
    m_Index = allocator.Alloc(&m_CpuHandle);
    m_CpuHandleSet = true;
    m_GpuHandleSet = false;
  }

  void AllocWithGpuHandle(DescriptorHeapListAllocator& allocator)
  {
    m_Index = allocator.Alloc(&m_CpuHandle, &m_GpuHandle);
    m_CpuHandleSet = true;
    m_GpuHandleSet = true;
  }

  UINT Index() const { return m_Index; }

  D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle() const
  {
    if (!m_CpuHandleSet) throw std::runtime_error("CPU Handle not set");
    return m_CpuHandle;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle() const
  {
    if (!m_GpuHandleSet) throw std::runtime_error("GPU Handle not set");
    return m_GpuHandle;
  }

private:
  UINT m_Index;
  D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
  D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
  bool m_CpuHandleSet = false;
  bool m_GpuHandleSet = false;
};

struct GpuBuffer {
  ID3D12Resource* Resource() const { return resource.Get(); };

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(size_t offset = 0) const { return resource->GetGPUVirtualAddress() + offset; }

  void AllocSrvDescriptor(DescriptorHeapListAllocator& allocator) { srvDescriptor.Alloc(allocator); }

  void AllocSrvDescriptorWithGpuHandle(DescriptorHeapListAllocator& allocator) { srvDescriptor.AllocWithGpuHandle(allocator); }

  void AllocUavDescriptor(DescriptorHeapListAllocator& allocator) { uavDescriptor.Alloc(allocator); }

  void AllocUavDescriptorWithGpuHandle(DescriptorHeapListAllocator& allocator) { uavDescriptor.AllocWithGpuHandle(allocator); }

  void AllocUavReadWriteDescriptor(DescriptorHeapListAllocator& allocator)
  {
    uavReadWriteDescriptor.Alloc(allocator);
  }

  void AllocRtvDescriptor(DescriptorHeapListAllocator& allocator) { rtvDescriptor.Alloc(allocator); }

  UINT SrvDescriptorIndex() const { return srvDescriptor.Index(); }

  UINT UavDescriptorIndex() const { return uavDescriptor.Index(); }

  D3D12_CPU_DESCRIPTOR_HANDLE SrvDescriptorHandle() const { return srvDescriptor.CpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE SrvDescriptorGpuHandle() const { return srvDescriptor.GpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE UavDescriptorHandle() const { return uavDescriptor.CpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE UavReadWriteDescriptorHandle() const { return uavReadWriteDescriptor.CpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE UavDescriptorGpuHandle() const { return uavDescriptor.GpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE RtvDescriptorHandle() const { return rtvDescriptor.CpuHandle(); }

  void Attach(ID3D12Resource* other) { resource.Attach(other); }

  void CreateResource(D3D12MA::Allocator* allocator, const D3D12MA::ALLOCATION_DESC* allocDesc,
                      const D3D12_RESOURCE_DESC* pResourceDesc,
                      D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON,
                      const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr)
  {
    currentState = InitialResourceState;
    CHECK_HR(allocator->CreateResource(allocDesc, pResourceDesc, InitialResourceState, pOptimizedClearValue,
                                       &allocation, IID_PPV_ARGS(&resource)));
  }

  void Map() { CHECK_HR(resource->Map(0, &EMPTY_RANGE, &address)); }

  void Map(D3D12_RANGE *pReadRange, void** ppData) { CHECK_HR(resource->Map(0, pReadRange, ppData)); }

  void MapOpaque() { CHECK_HR(resource->Map(0, &EMPTY_RANGE, nullptr)); }

  void Unmap() { resource->Unmap(0, nullptr); }

  void Clear(size_t size) { ZeroMemory((BYTE*)address, size); }

  void Copy(size_t offset, const void* data, size_t size) { memcpy((BYTE*)address + offset, data, size); }

  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT DstSubresource = 0)
  {
    resource->WriteToSubresource(DstSubresource, nullptr, data->pData, data->RowPitch, data->SlicePitch);
  }

  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource = 0)
  {
    for (UINT i = 0; i < numSubresources; ++i) {
      resource->WriteToSubresource(firstSubresource + i, nullptr, data[i].pData, data[i].RowPitch, data[i].SlicePitch);
    }
  }

  D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
  {
    // TODO: implement state tracking assert(currentState == stateBefore);
    // maybe implement thin commandlist/queue wrapper?
    currentState = stateAfter;
    return CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), stateBefore, stateAfter);
  }

  void SetName(std::wstring name)
  {
    std::wstring allocName = name + L" (Allocation)";

    resourceName = name;
    resource->SetName(name.c_str());
    allocation->SetName(allocName.c_str());
  }

  void Reset()
  {
    resource.Reset();
    if (allocation) {
      allocation->Release();
      allocation = nullptr;
    }
  }

private:
  ComPtr<ID3D12Resource> resource;
  HeapDescriptor srvDescriptor;
  HeapDescriptor uavDescriptor;
  HeapDescriptor uavReadWriteDescriptor;
  HeapDescriptor rtvDescriptor;
  D3D12MA::Allocation* allocation = nullptr;
  void* address = nullptr;
  D3D12_RESOURCE_STATES currentState;
  std::wstring resourceName;
};

struct Material {
  MaterialData m_GpuData;

  UINT m_MaterialBufferOffset;

  UINT MaterialIndex() const { return m_MaterialBufferOffset / sizeof(m_GpuData); }
};

struct FrameContext {
  FrameConstants frameConstants;

  BuffersDescriptorIndices buffersDescriptorsIndices;
  SkinningBuffersDescriptorIndices skinningBuffersDescriptorsIndices;
  CullingBuffersDescriptorIndices cullingBuffersDescriptorsIndices;

  static constexpr size_t frameConstantsSize = SizeOfInUint(frameConstants);

  GpuBuffer renderTarget;
  GpuBuffer timestampReadBackBuffer;

  ComPtr<ID3D12CommandAllocator> commandAllocator;
  ComPtr<ID3D12Fence> fence;
  UINT64 fenceValue;

  void Reset() {
    renderTarget.Reset();
    timestampReadBackBuffer.Reset();
    commandAllocator.Reset();
    fence.Reset();
  }
};

struct MeshStore {
  // Vertex data
  UINT CopyPositions(const void* data, size_t size)
  {
    // TODO: should ensure it is mapped
    UINT offset = m_CurrentOffsets.positionsBuffer;
    m_VertexPositions.Copy(offset, data, size);
    m_CurrentOffsets.positionsBuffer += size;

    return offset;
  }

  UINT ReservePositions(size_t size)
  {
    UINT offset = m_CurrentOffsets.positionsBuffer;
    m_CurrentOffsets.positionsBuffer += size;

    return offset;
  }

  UINT CopyNormals(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.normalsBuffer;
    m_VertexNormals.Copy(offset, data, size);
    m_CurrentOffsets.normalsBuffer += size;

    return offset;
  }

  UINT ReserveNormals(size_t size)
  {
    UINT offset = m_CurrentOffsets.normalsBuffer;
    m_CurrentOffsets.normalsBuffer += size;

    return offset;
  }

  UINT CopyTangents(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.tangentsBuffer;
    m_VertexTangents.Copy(offset, data, size);
    m_CurrentOffsets.tangentsBuffer += size;

    return offset;
  }

  UINT ReserveTangents(size_t size)
  {
    UINT offset = m_CurrentOffsets.tangentsBuffer;
    m_CurrentOffsets.tangentsBuffer += size;

    return offset;
  }

  UINT CopyUVs(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.uvsBuffer;
    m_VertexUVs.Copy(offset, data, size);
    m_CurrentOffsets.uvsBuffer += size;

    return offset;
  }

  UINT CopyBWI(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.bwiBuffer;
    m_VertexBlendWeightsAndIndices.Copy(offset, data, size);
    m_CurrentOffsets.bwiBuffer += size;

    return offset;
  }

  // Meshlet data

  UINT CopyMeshlets(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.meshletsBuffer;
    m_Meshlets.Copy(offset, data, size);
    m_CurrentOffsets.meshletsBuffer += size;

    return offset;
  }

  UINT CopyMeshletUniqueIndices(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.uniqueIndicesBuffer;
    m_MeshletUniqueIndices.Copy(offset, data, size);
    m_CurrentOffsets.uniqueIndicesBuffer += size;

    return offset;
  }

  UINT CopyMeshletPrimitives(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.primitivesBuffer;
    m_MeshletPrimitives.Copy(offset, data, size);
    m_CurrentOffsets.primitivesBuffer += size;

    return offset;
  }

  // meta data

  UINT CopyMaterial(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.materialsBuffer;
    m_Materials.Copy(offset, data, size);
    m_CurrentOffsets.materialsBuffer += size;

    return offset;
  }

  UINT ReserveInstance(size_t size)
  {
    UINT offset = m_CurrentOffsets.instancesBuffer;
    m_CurrentOffsets.instancesBuffer += size;

    return offset;
  }

  void UpdateInstances(const void* data, size_t size, UINT offset, UINT frameIndex)
  {
    m_Instances[frameIndex].Copy(offset, data, size);
  }

  UINT ReserveBoneMatrices(size_t size)
  {
    UINT offset = m_CurrentOffsets.boneMatricesBuffer;
    m_CurrentOffsets.boneMatricesBuffer += size;

    return offset;
  }

  void UpdateBoneMatrices(const void* data, size_t size, UINT offset, UINT frameIndex)
  {
    m_BoneMatrices[frameIndex].Copy(offset, data, size);
  }

  BuffersDescriptorIndices BuffersDescriptorIndices(UINT frameIndex) const
  {
    return {
        .vertexPositionsBufferId = m_VertexPositions.SrvDescriptorIndex(),
        .vertexNormalsBufferId = m_VertexNormals.SrvDescriptorIndex(),
        .vertexTangentsBufferId = m_VertexTangents.SrvDescriptorIndex(),
        .vertexUVsBufferId = m_VertexUVs.SrvDescriptorIndex(),

        .meshletsBufferId = m_Meshlets.SrvDescriptorIndex(),
        .meshletVertIndicesBufferId = m_MeshletUniqueIndices.SrvDescriptorIndex(),
        .meshletsPrimitivesBufferId = m_MeshletPrimitives.SrvDescriptorIndex(),

        .materialsBufferId = m_Materials.SrvDescriptorIndex(),
        .instancesBufferId = m_Instances[frameIndex].SrvDescriptorIndex(),
    };
  }

  SkinningBuffersDescriptorIndices SkinningBuffersDescriptorIndices(UINT frameIndex) const
  {
    return {
        .vertexPositionsBufferId = m_VertexPositions.UavDescriptorIndex(),
        .vertexNormalsBufferId = m_VertexNormals.UavDescriptorIndex(),
        .vertexTangentsBufferId = m_VertexTangents.UavDescriptorIndex(),
        .vertexBlendWeightsAndIndicesBufferId = m_VertexBlendWeightsAndIndices.SrvDescriptorIndex(),
        .boneMatricesBufferId = m_BoneMatrices[frameIndex].SrvDescriptorIndex(),
    };
  }

  UINT InstancesBufferId(UINT frameIndex) const { return m_Instances[frameIndex].SrvDescriptorIndex(); }

  GpuBuffer m_VertexPositions;
  GpuBuffer m_VertexNormals;
  GpuBuffer m_VertexTangents;
  GpuBuffer m_VertexUVs;
  GpuBuffer m_VertexBlendWeightsAndIndices;

  GpuBuffer m_Meshlets;
  GpuBuffer m_MeshletUniqueIndices;
  GpuBuffer m_MeshletPrimitives;

  GpuBuffer m_Materials;
  GpuBuffer m_Instances[FRAME_BUFFER_COUNT];  // updated by CPU

  GpuBuffer m_BoneMatrices[FRAME_BUFFER_COUNT];  // updated by CPU

  struct {
    // vertex data
    UINT positionsBuffer = 0;
    UINT normalsBuffer = 0;
    UINT tangentsBuffer = 0;
    UINT uvsBuffer = 0;
    UINT bwiBuffer = 0;

    // meshlet data
    UINT meshletsBuffer = 0;
    UINT visibleMeshletsBuffer = 0;
    UINT uniqueIndicesBuffer = 0;
    UINT primitivesBuffer = 0;

    // meta data
    UINT materialsBuffer = 0;
    UINT instancesBuffer = 0;

    UINT boneMatricesBuffer = 0;
  } m_CurrentOffsets;
};

// ========== Static functions declarations

static void InitD3D();
static void InitFrameResources();
static void WaitForFrame(FrameContext* ctx);
static void MoveToNextFrame();
static void WaitGPUIdle();
static std::wstring GetAssetFullPath(LPCWSTR assetName);
static void PrintAdapterInformation(IDXGIAdapter1* adapter);
static std::shared_ptr<MeshInstance> LoadMesh3D(std::shared_ptr<Mesh3D> mesh);
static UINT CreateTexture(std::filesystem::path filename);

// ========== Global variables

static UINT g_Width;
static UINT g_Height;
static float g_AspectRatio;
static bool g_Raster = true;

static std::wstring g_Title;
static std::wstring g_AssetsPath;

// Pipeline objects
static IDXGIFactory4* g_Factory = nullptr;
static ComPtr<IDXGIAdapter1> g_Adapter;

static ComPtr<ID3D12Device5> g_Device;
static DXGI_ADAPTER_DESC1 g_AdapterDesc;
static ComPtr<D3D12MA::Allocator> g_Allocator;
// Used only when ENABLE_CPU_ALLOCATION_CALLBACKS
static D3D12MA::ALLOCATION_CALLBACKS g_AllocationCallbacks;

// swapchain used to switch between render targets
static ComPtr<IDXGISwapChain3> g_SwapChain;
// container for command lists
static ComPtr<ID3D12CommandQueue> g_CommandQueue;
static ComPtr<ID3D12GraphicsCommandList6> g_CommandList;

static FrameContext g_FrameContext[FRAME_BUFFER_COUNT];
static UINT g_FrameIndex;
static HANDLE g_FenceEvent;

// Resources
static ComPtr<ID3D12DescriptorHeap> g_SrvUavDescriptorHeap;
static ComPtr<ID3D12DescriptorHeap> g_UavReadWriteDescriptorHeap;
static DescriptorHeapListAllocator g_SrvUavDescHeapAlloc;
static DescriptorHeapListAllocator g_UavReadWriteDescHeapAlloc;

static ComPtr<ID3D12DescriptorHeap> g_RtvDescriptorHeap;
static DescriptorHeapListAllocator g_RtvDescHeapAlloc;

static GpuBuffer g_DepthStencilBuffer;
static ComPtr<ID3D12DescriptorHeap> g_DepthStencilDescriptorHeap;

static ComPtr<ID3D12QueryHeap> g_TimestampQueryHeap;

// PSO
static std::unordered_map<PSO, ComPtr<ID3D12PipelineState>> g_PipelineStateObjects;
static ComPtr<ID3D12RootSignature> g_RootSignature;
static ComPtr<ID3D12RootSignature> g_ComputeRootSignature;
static ComPtr<ID3D12CommandSignature> g_DrawMeshCommandSignature;

static GpuBuffer g_DrawMeshCommands;      // written by compute shader
static GpuBuffer g_UAVCounterReset;

static GpuBuffer g_VisibilityBuffer;

struct GBuffer {
  GpuBuffer worldPosition;
  GpuBuffer worldNormal;
  GpuBuffer baseColor;

  FillGBufferPerDispatchConstants PerDispatchConstants(UINT visBufferDescId)
  {
    return {
        .VisibilityBufferId = visBufferDescId,
        .WorldPositionId = worldPosition.UavDescriptorIndex(),
        .WorldNormalId = worldNormal.UavDescriptorIndex(),
        .BaseColorId = baseColor.UavDescriptorIndex(),
    };
  }

  void Reset()
  {
    worldPosition.Reset();
    worldNormal.Reset();
    baseColor.Reset();
  }
};

static GBuffer g_GBuffer;

static MeshStore g_MeshStore;
static std::unordered_map<std::wstring, std::shared_ptr<Material>> g_MaterialMap;
static std::unordered_map<std::wstring, std::shared_ptr<GpuBuffer>> g_Textures;
static Scene g_Scene;

static std::atomic<size_t> g_CpuAllocationCount{0};

// ========== Public functions

void InitWindow(UINT width, UINT height, std::wstring name)
{
  g_Width = width;
  g_Height = height;
  g_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
}

void InitAdapter(IDXGIFactory4* factory, IDXGIAdapter1* adapter)
{
  g_Factory = factory;
  assert(g_Factory);

  g_Adapter = adapter;
  assert(g_Adapter);

  CHECK_HR(g_Adapter->GetDesc1(&g_AdapterDesc));
}

void Init()
{
  InitD3D();
  InitFrameResources();
}

void LoadAssets()
{
  for (auto &node : g_Scene.nodes) {
    for (auto &mesh : node.model->meshes) {
      auto mi = LoadMesh3D(mesh);

      node.meshInstances.push_back(mi);
      if (mi->skinnedMeshInstance) {
        node.skinnedMeshInstances.push_back(mi->skinnedMeshInstance);
      }
    }
  }
}

void Update(float time, float dt)
{
  auto ctx = &g_FrameContext[g_FrameIndex];

  // Per frame root constants
  {
    ctx->frameConstants.Time = time;
    ctx->frameConstants.CameraWS = g_Scene.camera->WorldPos();
    ctx->frameConstants.ScreenSize = {static_cast<float>(g_Width), static_cast<float>(g_Height)};
  }

  // Per object constant buffer
  {
    std::vector<MeshInstanceData> tmpInstances(g_Scene.numMeshInstances);
    std::vector<XMFLOAT4X4> tmpBoneMatrices(g_Scene.numBoneMatrices);

    const XMMATRIX projection = XMMatrixPerspectiveFovRH(45.f * (XM_PI / 180.f), g_AspectRatio, 0.1f, 1000.f);

    XMMATRIX view = g_Scene.camera->LookAt();
    XMMATRIX viewProjection = view * projection;

    // Extract planes for frustum culling
    XMMATRIX vp = XMMatrixTranspose(viewProjection);
    std::array<XMVECTOR, 6> planes = {
        XMPlaneNormalize(vp.r[3] + vp.r[0]),  // Left
        XMPlaneNormalize(vp.r[3] - vp.r[0]),  // Right
        XMPlaneNormalize(vp.r[3] + vp.r[1]),  // Bottom
        XMPlaneNormalize(vp.r[3] - vp.r[1]),  // Top
        XMPlaneNormalize(vp.r[2]),            // Near
        XMPlaneNormalize(vp.r[3] - vp.r[2]),  // Far
    };

    for (size_t i = 0; i < planes.size(); i++) {
      XMStoreFloat4(&ctx->frameConstants.FrustumPlanes[i], planes[i]);
    }

    for (auto& node : g_Scene.nodes) {
      auto model = node.model;

      if (model->HasCurrentAnimation()) {
        size_t i = 0;
        for (auto &[k, skin] : model->skins) {
          std::vector<XMFLOAT4X4> matrices = model->currentAnimation.BoneTransforms(dt, skin.get());

          for (auto &smi : node.skinnedMeshInstances) {
            // TODO: should reuse bone matrice buffer for meshes of same model which share skin
            // TODO: should also update the collision data...
            std::copy(matrices.begin(), matrices.end(), tmpBoneMatrices.begin() + smi->offsets.boneMatricesBuffer);
          }
        }
      }  // else identity matrices ?

      XMMATRIX modelMat = model->WorldMatrix();

      for (auto mi : node.meshInstances) {
        XMMATRIX world = mi->mesh->LocalTransformMatrix() * modelMat;

        if (model->HasCurrentAnimation() && mi->mesh->parentBone > -1) {
          auto boneMatrix = model->currentAnimation.globalTransforms[mi->mesh->parentBone];

          world = mi->mesh->LocalTransformMatrix() * boneMatrix * modelMat;
        }

        XMMATRIX worldViewProjection = world * viewProjection;
        XMMATRIX normalMatrix = XMMatrixInverse(nullptr, world);

        XMStoreFloat4x4(&mi->data.worldViewProj, XMMatrixTranspose(worldViewProjection));
        XMStoreFloat4x4(&mi->data.worldMatrix, XMMatrixTranspose(world));
        XMStoreFloat3x3(&mi->data.normalMatrix, normalMatrix);
        mi->data.boundingSphere =
            XMFLOAT4(mi->mesh->boundingSphere.Center.x, mi->mesh->boundingSphere.Center.y,
                     mi->mesh->boundingSphere.Center.z, mi->mesh->boundingSphere.Radius);

        XMVECTOR scale, rot, pos;
        XMMatrixDecompose(&scale, &rot, &pos, world);
        mi->data.scale = XMVectorGetX(scale);

        tmpInstances[mi->instanceBufferOffset / sizeof(MeshInstance::data)] = mi->data;
      }
    }

    if (g_Scene.numBoneMatrices > 0) {
      g_MeshStore.UpdateBoneMatrices(tmpBoneMatrices.data(), g_Scene.numBoneMatrices * sizeof(XMFLOAT4X4), 0,
                                     g_FrameIndex);
    }
    g_MeshStore.UpdateInstances(tmpInstances.data(), g_Scene.numMeshInstances * sizeof(MeshInstance::data), 0, g_FrameIndex);
  }

  // ImGui
  {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
  }

  {
    ImGui::Begin("Ray tracing");
    ImGui::Checkbox("Raster", &g_Raster);
    ImGui::End();
  }

  {
    ImGui::Begin("Timestamps");

    UINT64* timestamps = nullptr;
    ctx->timestampReadBackBuffer.Map(nullptr, (void**)&timestamps);

    UINT64 frequency;
    g_CommandQueue->GetTimestampFrequency(&frequency);

    auto GetTime = [&frequency, &timestamps](size_t i) {
      UINT64 begin = timestamps[i];
      UINT64 end = timestamps[i+1];
      UINT64 delta = end - begin;

      return static_cast<double>(delta) / frequency * 1000.0;
    };

    ImGui::Text("Skinning: %.4f ms", GetTime(Timestamp::SkinBegin));
    ImGui::Text("Culling: %.4f ms", GetTime(Timestamp::CullBegin));
    ImGui::Text("Raster: %.4f ms", GetTime(Timestamp::DrawBegin));
    ImGui::Text("Fill G-Buffer: %.4f ms", GetTime(Timestamp::FillGBufferBegin));
    ImGui::Text("Total: %.4f ms", GetTime(Timestamp::TotalBegin));

    ctx->timestampReadBackBuffer.Unmap();

    ImGui::End();
  }

  {
    float scale = 0.25;
    auto imgSize = ImVec2((float)g_Width * scale, (float)g_Height * scale);

    ImGui::Begin("GBuffer viewer");

    if (ImGui::BeginTabBar("GBufferTabs")) {
      if (ImGui::BeginTabItem("Normal")) {
        ImGui::Image((ImTextureID)g_GBuffer.worldNormal.SrvDescriptorGpuHandle().ptr, imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Position")) {
        ImGui::Image((ImTextureID)g_GBuffer.worldPosition.SrvDescriptorGpuHandle().ptr, imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Base Color")) {
        ImGui::Image((ImTextureID)g_GBuffer.baseColor.SrvDescriptorGpuHandle().ptr, imgSize);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }
}

void Render()
{
  auto ctx = &g_FrameContext[g_FrameIndex];

  // we can only reset an allocator once the gpu is done with it. Resetting an
  // allocator frees the memory that the command list was stored in
  CHECK_HR(ctx->commandAllocator->Reset());

  // reset the command list. by resetting the command list we are putting it
  // into a recording state so we can start recording commands into the command
  // allocator. The command allocator that we reference here may have multiple
  // command lists associated with it, but only one can be recording at any
  // time. Make sure that any other command lists associated to this command
  // allocator are in the closed state (not recording).
  CHECK_HR(g_CommandList->Reset(ctx->commandAllocator.Get(), g_PipelineStateObjects[PSO::SkinningCS].Get()));

  // here we start recording commands into the g_CommandList (which all the
  // commands will be stored in the g_CommandAllocators)
  g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::TotalBegin);

  // transition the "g_FrameIndex" render target from the present state to the
  // render target state so the command list draws to it starting from here
  std::array<D3D12_RESOURCE_BARRIER, 2> preRenderBarriers;
  preRenderBarriers[0] = ctx->renderTarget.Transition(D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  preRenderBarriers[1] =
      g_VisibilityBuffer.Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
  g_CommandList->ResourceBarrier(preRenderBarriers.size(), preRenderBarriers.data());

  // here we again get the handle to our current render target view so we can
  // set it as the render target in the output merger stage of the pipeline
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
      g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

  g_CommandList->ClearDepthStencilView(
      g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
      D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  static constexpr float visBufferClearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
  g_CommandList->ClearRenderTargetView(g_VisibilityBuffer.RtvDescriptorHandle(), visBufferClearColor, 0, nullptr);

  // Clear the render target by using the ClearRenderTargetView command
  if (g_Raster) {
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(ctx->renderTarget.RtvDescriptorHandle(), clearColor, 0, nullptr);
  } else {
    const float clearColor[] = {0.6f, 0.8f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(ctx->renderTarget.RtvDescriptorHandle(), clearColor, 0, nullptr);
  }

  std::array descriptorHeaps{g_SrvUavDescriptorHeap.Get()};
  g_CommandList->SetDescriptorHeaps(descriptorHeaps.size(), descriptorHeaps.data());

  D3D12_VIEWPORT viewport{0.f, 0.f, (float)g_Width, (float)g_Height, 0.f, 1.f};
  g_CommandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissorRect{0, 0, g_Width, g_Height};
  g_CommandList->RSSetScissorRects(1, &scissorRect);

  // record skinning compute commands if needed
  // TODO: we should also update culling data. And move to Indirect?
  if (g_Scene.skinnedMeshInstances.size() > 0) {
    g_CommandList->SetComputeRootSignature(g_ComputeRootSignature.Get());

    g_CommandList->SetComputeRoot32BitConstants(SkinningCSRootParameter::BuffersDescriptorIndices,
                                                SizeOfInUint(SkinningBuffersDescriptorIndices),
                                                &ctx->skinningBuffersDescriptorsIndices, 0);
    auto b0 = g_MeshStore.m_VertexPositions.Transition(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    g_CommandList->ResourceBarrier(1, &b0);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::SkinBegin);
    for (auto smi : g_Scene.skinnedMeshInstances) {
      auto o = smi->BuffersOffsets();
      g_CommandList->SetComputeRoot32BitConstants(SkinningCSRootParameter::BuffersOffsets,
                                                  SizeOfInUint(SkinningPerDispatchConstants), &o, 0);

      g_CommandList->Dispatch(DivRoundUp(smi->numVertices, COMPUTE_GROUP_SIZE), 1, 1);
    }
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::SkinEnd);

    auto b1 = g_MeshStore.m_VertexPositions.Transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g_CommandList->ResourceBarrier(1, &b1);
  }

  // record culling commands
  {
    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::InstanceCullingCS].Get());

    g_CommandList->SetComputeRootSignature(g_RootSignature.Get());

    g_CommandList->SetComputeRoot32BitConstants(RootParameter::FrameConstants, FrameContext::frameConstantsSize,
                                                 &ctx->frameConstants, 0);

    g_CommandList->SetComputeRoot32BitConstants(RootParameter::BuffersDescriptorIndices,
                                                SizeOfInUint(CullingBuffersDescriptorIndices),
                                                &ctx->cullingBuffersDescriptorsIndices, 0);

    g_CommandList->SetComputeRoot32BitConstant(RootParameter::PerDrawConstants, g_Scene.numMeshInstances, 0);

    g_CommandList->CopyBufferRegion(g_DrawMeshCommands.Resource(), DRAW_MESH_CMDS_COUNTER_OFFSET,
                                    g_UAVCounterReset.Resource(), 0, sizeof(UINT));

    auto before = g_DrawMeshCommands.Transition(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    g_CommandList->ResourceBarrier(1, &before);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::CullBegin);
    g_CommandList->Dispatch(DivRoundUp(g_Scene.numMeshInstances, COMPUTE_GROUP_SIZE), 1, 1);
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::CullEnd);

    auto after =
        g_DrawMeshCommands.Transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    g_CommandList->ResourceBarrier(1, &after);
  }

  // Record drawing commands
  {
    auto rtvHandle = g_VisibilityBuffer.RtvDescriptorHandle();
    g_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::BasicMS].Get());

    g_CommandList->SetGraphicsRootSignature(g_RootSignature.Get());

    g_CommandList->SetGraphicsRoot32BitConstants(RootParameter::FrameConstants, FrameContext::frameConstantsSize,
                                                 &ctx->frameConstants, 0);
    g_CommandList->SetGraphicsRoot32BitConstants(RootParameter::BuffersDescriptorIndices,
                                                 SizeOfInUint(BuffersDescriptorIndices),
                                                 &ctx->buffersDescriptorsIndices,
                                                 0);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::DrawBegin);
    g_CommandList->ExecuteIndirect(g_DrawMeshCommandSignature.Get(), MESH_INSTANCE_COUNT, g_DrawMeshCommands.Resource(),
                                   0, g_DrawMeshCommands.Resource(), DRAW_MESH_CMDS_COUNTER_OFFSET);
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::DrawEnd);

    auto after =
        g_VisibilityBuffer.Transition(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    g_CommandList->ResourceBarrier(1, &after);
  }

  // Record Fill G-Buffer from Visibility-Buffer commands
  {
    std::array<D3D12_RESOURCE_BARRIER, 3> before;
    before[0] = g_GBuffer.worldPosition.Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    before[1] = g_GBuffer.worldNormal.Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    before[2] = g_GBuffer.baseColor.Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    g_CommandList->ResourceBarrier(before.size(), before.data());

    static constexpr float clearFloat[] = {0.0f, 0.0f, 0.0f, 0.0f};
    static constexpr UINT clearUint[] = {0, 0, 0, 0};

    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::FillGBufferCS].Get());

    auto c = g_GBuffer.PerDispatchConstants(g_VisibilityBuffer.SrvDescriptorIndex());
    auto n = SizeOfInUint(c);
    g_CommandList->SetComputeRoot32BitConstants(RootParameter::PerDrawConstants, n, &c, 0);

    g_CommandList->SetComputeRoot32BitConstants(RootParameter::BuffersDescriptorIndices,
                                                SizeOfInUint(BuffersDescriptorIndices), &ctx->buffersDescriptorsIndices, 0);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::FillGBufferBegin);
    g_CommandList->Dispatch(DivRoundUp(g_Width, FILL_GBUFFER_GROUP_SIZE_X), DivRoundUp(g_Height, FILL_GBUFFER_GROUP_SIZE_Y), 1);
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::FillGBufferEnd);

    std::array<D3D12_RESOURCE_BARRIER, 3> after;
    after[0] = g_GBuffer.worldPosition.Transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    after[1] = g_GBuffer.worldNormal.Transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    after[2] = g_GBuffer.baseColor.Transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    g_CommandList->ResourceBarrier(after.size(), after.data());
  }

  // Record Full screen triangle pass - Compose final image commands
  {
    auto rtvHandle = ctx->renderTarget.RtvDescriptorHandle();
    g_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::FinalComposeVS].Get());

    g_CommandList->SetGraphicsRoot32BitConstant(RootParameter::PerDrawConstants,
                                                g_GBuffer.baseColor.SrvDescriptorIndex(), 0);

    g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_CommandList->DrawInstanced(3, 1, 0, 0);
  }

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_CommandList.Get());

  // transition the "g_FrameIndex" render target from the render target state to
  // the present state. If the debug layer is enabled, you will receive a
  // warning if present is called on the render target when it's not in the
  // present state
  std::array<D3D12_RESOURCE_BARRIER, 2> postRenderBarriers;
  postRenderBarriers[0] =
      ctx->renderTarget.Transition(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  postRenderBarriers[1] =
      g_DrawMeshCommands.Transition(D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);

  g_CommandList->ResourceBarrier(postRenderBarriers.size(), postRenderBarriers.data());

  g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::TotalEnd);

  g_CommandList->ResolveQueryData(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, Timestamp::Count,
                                  ctx->timestampReadBackBuffer.Resource(), 0);

  CHECK_HR(g_CommandList->Close());

  // ==========

  // execute command list
  std::array ppCommandLists{static_cast<ID3D12CommandList*>(g_CommandList.Get())};
  g_CommandQueue->ExecuteCommandLists(ppCommandLists.size(), ppCommandLists.data());

  // present the current backbuffer
  CHECK_HR(g_SwapChain->Present(PRESENT_SYNC_INTERVAL, 0));

  MoveToNextFrame();
}

void Cleanup()
{
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  // wait for the gpu to finish all frames
  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    // TODO: Method from FrameContext
    auto ctx = &g_FrameContext[i];

    CHECK_HR(g_CommandQueue->Signal(ctx->fence.Get(), ctx->fenceValue));
    WaitForFrame(ctx);
  }

  // get swapchain out of full screen before exiting
  BOOL fs = false;
  CHECK_HR(g_SwapChain->GetFullscreenState(&fs, NULL));

  if (fs) g_SwapChain->SetFullscreenState(false, NULL);

  WaitGPUIdle();

  for (auto& [k, tex] : g_Textures) {
    g_SrvUavDescHeapAlloc.Free(tex->SrvDescriptorIndex());
    tex->Reset();
  }

  {
    g_MeshStore.m_VertexPositions.Unmap();
    g_MeshStore.m_VertexPositions.Reset();

    g_MeshStore.m_VertexNormals.Unmap();
    g_MeshStore.m_VertexNormals.Reset();

    g_MeshStore.m_VertexTangents.Unmap();
    g_MeshStore.m_VertexTangents.Reset();

    g_MeshStore.m_VertexUVs.Unmap();
    g_MeshStore.m_VertexUVs.Reset();

    g_MeshStore.m_VertexBlendWeightsAndIndices.Unmap();
    g_MeshStore.m_VertexBlendWeightsAndIndices.Reset();

    g_MeshStore.m_Meshlets.Unmap();
    g_MeshStore.m_Meshlets.Reset();

    g_MeshStore.m_MeshletUniqueIndices.Unmap();
    g_MeshStore.m_MeshletUniqueIndices.Reset();

    g_MeshStore.m_MeshletPrimitives.Unmap();
    g_MeshStore.m_MeshletPrimitives.Reset();

    g_MeshStore.m_Materials.Unmap();
    g_MeshStore.m_Materials.Reset();

    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      g_MeshStore.m_Instances[i].Unmap();
      g_MeshStore.m_Instances[i].Reset();

      g_MeshStore.m_BoneMatrices[i].Unmap();
      g_MeshStore.m_BoneMatrices[i].Reset();
    }
  }

  g_PipelineStateObjects[PSO::BasicMS].Reset();
  g_PipelineStateObjects[PSO::SkinningCS].Reset();
  g_PipelineStateObjects[PSO::InstanceCullingCS].Reset();
  g_PipelineStateObjects[PSO::FillGBufferCS].Reset();
  g_PipelineStateObjects[PSO::FinalComposeVS].Reset();
  g_RootSignature.Reset();
  g_DrawMeshCommandSignature.Reset();

  g_DrawMeshCommands.Reset();
  g_UAVCounterReset.Reset();

  g_VisibilityBuffer.Reset();
  g_GBuffer.Reset();

  CloseHandle(g_FenceEvent);
  g_CommandList.Reset();
  g_CommandQueue.Reset();

  g_SrvUavDescriptorHeap.Reset();
  g_RtvDescriptorHeap.Reset();
  g_DepthStencilBuffer.Reset();
  g_DepthStencilDescriptorHeap.Reset();

  for (size_t i = FRAME_BUFFER_COUNT; i--;) {
    g_FrameContext[i].Reset();
  }

  PrintStatsString();

  g_Allocator.Reset();

  if (ENABLE_CPU_ALLOCATION_CALLBACKS) {
    assert(g_CpuAllocationCount.load() == 0);
  }

  g_Device.Reset();
  g_SwapChain.Reset();
}

void PrintStatsString()
{
  WCHAR* statsString = NULL;
  g_Allocator->BuildStatsString(&statsString, TRUE);
  wprintf(L"%s\n", statsString);
  g_Allocator->FreeStatsString(statsString);
}

UINT GetWidth() { return g_Width; }

UINT GetHeight() { return g_Height; }

const WCHAR* GetTitle() { return g_Title.c_str(); }

void SetSceneCamera(Camera* cam) { g_Scene.camera = cam; }

void AppendToScene(Model3D* model)
{
  Scene::SceneNode node;
  node.model = model;

  g_Scene.nodes.push_back(node);
}

UINT CreateMaterial(std::filesystem::path baseDir, std::wstring filename)
{
  // TODO: CreateTextures won't work from here. split it to ? CreateTexture + UploadTexture
  std::filesystem::path materialPath = baseDir / filename;
  auto it = g_MaterialMap.find(materialPath);
  if (it != g_MaterialMap.end()) return it->second->MaterialIndex();

  auto material = std::make_shared<Material>();

  std::ifstream file(materialPath);
  std::string line;

  // base color
  std::getline(file, line);
  std::filesystem::path baseColorPath = baseDir / line;
  material->m_GpuData.baseColorId = CreateTexture(baseColorPath);

  // metallic roughness
  std::getline(file, line);
  std::filesystem::path metallicRoughnessPath = baseDir / line;
  material->m_GpuData.metallicRoughnessId = CreateTexture(metallicRoughnessPath);

  // normal map
  std::getline(file, line);
  std::filesystem::path normalMapPath = baseDir / line;
  material->m_GpuData.normalMapId = CreateTexture(normalMapPath);

  material->m_MaterialBufferOffset = g_MeshStore.CopyMaterial(&material->m_GpuData, sizeof(material->m_GpuData));

  g_MaterialMap[materialPath] = material;

  return material->MaterialIndex();
}

// ========== Static functions

static void* CustomAllocate(size_t Size, size_t Alignment, void* pPrivateData)
{
  assert(pPrivateData == CUSTOM_ALLOCATION_PRIVATE_DATA);

  void* memory = _aligned_malloc(Size, Alignment);

  if (ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT) {
    wprintf(L"Allocate Size=%llu Alignment=%llu -> %p\n", Size, Alignment,
            memory);
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

static void InitD3D()
{
#ifdef ENABLE_DEBUG_LAYER
  ID3D12Debug* debug;

  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    debug->EnableDebugLayer();
#endif

  // Create Device
  {
    ID3D12Device5* device = nullptr;
    CHECK_HR(D3D12CreateDevice(g_Adapter.Get(), FEATURE_LEVEL, IID_PPV_ARGS(&device)));
    g_Device.Attach(device);

    // Ray tracing capabilities
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    CHECK_HR(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));
    assert(options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);

    // Mesh shading
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    CHECK_HR(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)));
    assert(options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);

    // GPU Upload Heap Supported
    bool GPUUploadHeapSupported = false;
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options16 = {};
    CHECK_HR(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options16, sizeof(options16)));
    assert(options16.GPUUploadHeapSupported);
  }

#ifdef ENABLE_DEBUG_LAYER
  ID3D12InfoQueue* pInfoQueue = nullptr;
  g_Device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
  pInfoQueue->Release();
  debug->Release();
#endif

  // Create Memory allocator
  {
    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
    desc.pDevice = g_Device.Get();
    desc.pAdapter = g_Adapter.Get();

    if (ENABLE_CPU_ALLOCATION_CALLBACKS) {
      g_AllocationCallbacks.pAllocate = &CustomAllocate;
      g_AllocationCallbacks.pFree = &CustomFree;
      g_AllocationCallbacks.pPrivateData = CUSTOM_ALLOCATION_PRIVATE_DATA;
      desc.pAllocationCallbacks = &g_AllocationCallbacks;
    }

    CHECK_HR(D3D12MA::CreateAllocator(&desc, &g_Allocator));

    PrintAdapterInformation(g_Adapter.Get());
  }

  // Create Command Queue
  {
    D3D12_COMMAND_QUEUE_DESC cqDesc{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };

    ID3D12CommandQueue* commandQueue = nullptr;
    CHECK_HR(g_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)));
    g_CommandQueue.Attach(commandQueue);
  }

  // Create Command Allocator
  {
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      ID3D12CommandAllocator* commandAllocator = nullptr;
      CHECK_HR(g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
      g_FrameContext[i].commandAllocator.Attach(commandAllocator);
    }

    // create the command list with the first allocator
    CHECK_HR(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_FrameContext[0].commandAllocator.Get(),
                                         NULL, IID_PPV_ARGS(&g_CommandList)));

    // command lists are created in the recording state. our main loop will set
    // it up for recording again so close it now
    g_CommandList->Close();
  }

  // Create Synchronization objects
  {
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      ID3D12Fence* fence = nullptr;
      CHECK_HR(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
      g_FrameContext[i].fence.Attach(fence);
      g_FrameContext[i].fenceValue = 0;
    }

    g_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(g_FenceEvent);
  }

  // Create Swapchain
  {
    // this is to describe our display mode
    DXGI_MODE_DESC backBufferDesc = {};
    backBufferDesc.Width = g_Width;
    backBufferDesc.Height = g_Height;
    backBufferDesc.Format = RENDER_TARGET_FORMAT;

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = FRAME_BUFFER_COUNT;
    swapChainDesc.BufferDesc = backBufferDesc;
    // this says the pipeline will render to this swap chain
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // dxgi will discard the buffer (data) after we call present
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = Win32Application::GetHwnd();

    swapChainDesc.SampleDesc.Count = 1;  // our multi-sampling description
    swapChainDesc.SampleDesc.Quality = 0;

    swapChainDesc.Windowed = true;  // set to true, then if in fullscreen must
                                    // call SetFullScreenState with true for
                                    // full screen to get uncapped fps

    IDXGISwapChain* tempSwapChain;

    // The queue will be flushed once the swap chain is created. Give it the
    // swap chain description we created above and store the created swap chain
    // in a temp IDXGISwapChain interface
    CHECK_HR(g_Factory->CreateSwapChain(g_CommandQueue.Get(), &swapChainDesc,
                                        &tempSwapChain));

    g_SwapChain.Attach(static_cast<IDXGISwapChain3*>(tempSwapChain));

    g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
  }
}

static void InitFrameResources()
{
  // assets path
  WCHAR assetsPath[512];
  GetAssetsPath(assetsPath, _countof(assetsPath));
  g_AssetsPath = assetsPath;

  // RTV
  {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                           .NumDescriptors = FRAME_BUFFER_COUNT + 2,  // g_VisibilityBuffer + g_GBuffer
                                           .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
    CHECK_HR(g_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));
    g_RtvDescriptorHeap.Attach(rtvDescriptorHeap);

    g_RtvDescHeapAlloc.Create(g_Device.Get(), g_RtvDescriptorHeap.Get());

    // Create a RTV for each buffer (double buffering is two buffers, tripple buffering is 3).
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      // store the n'th buffer in the swap chain in the n'th position of render target
      ID3D12Resource* res = nullptr;
      CHECK_HR(g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&res)));
      g_FrameContext[i].renderTarget.Attach(res);

      g_FrameContext[i].renderTarget.AllocRtvDescriptor(g_RtvDescHeapAlloc);
      g_Device->CreateRenderTargetView(g_FrameContext[i].renderTarget.Resource(), nullptr,
                                       g_FrameContext[i].renderTarget.RtvDescriptorHandle());
    }
  }

  // DSV
  {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CHECK_HR(g_Device->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(&g_DepthStencilDescriptorHeap)));

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DEPTH_STENCIL_FORMAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    D3D12MA::CALLOCATION_DESC depthStencilAllocDesc =
        D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_DEFAULT};

    D3D12_RESOURCE_DESC depthStencilResourceDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(DEPTH_STENCIL_FORMAT, g_Width, g_Height);
    depthStencilResourceDesc.MipLevels = 1;
    depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    g_DepthStencilBuffer.CreateResource(
        g_Allocator.Get(), &depthStencilAllocDesc, &depthStencilResourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue);

    g_DepthStencilBuffer.SetName(L"Depth Stencil Buffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
    depthStencilDesc.Format = DEPTH_STENCIL_FORMAT;
    depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
    g_Device->CreateDepthStencilView(
        g_DepthStencilBuffer.Resource(), &depthStencilDesc,
        g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
  }

  // CBV_SRV_UAV descriptor heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = NUM_DESCRIPTORS_PER_HEAP,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    };
    CHECK_HR(g_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_SrvUavDescriptorHeap)));

    g_SrvUavDescHeapAlloc.Create(g_Device.Get(), g_SrvUavDescriptorHeap.Get());
  }

  // NON SHADER VISIBLE (for use with ClearUnorderedAccessView)
  {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = NUM_DESCRIPTORS_PER_HEAP,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };

    CHECK_HR(g_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_UavReadWriteDescriptorHeap)));

    g_UavReadWriteDescHeapAlloc.Create(g_Device.Get(), g_UavReadWriteDescriptorHeap.Get());
  }

  // Query heap
  {
    D3D12_QUERY_HEAP_DESC queryHeapDesc{
        .Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP,
        .Count = Timestamp::Count,
    };
    g_Device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&g_TimestampQueryHeap));
  }

  // Setup Platform/Renderer backends
  ImGui_ImplDX12_InitInfo initInfo = {};
  initInfo.Device = g_Device.Get();
  initInfo.CommandQueue = g_CommandQueue.Get();
  initInfo.NumFramesInFlight = FRAME_BUFFER_COUNT;
  initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
  // Allocating SRV descriptors (for textures) is up to the application, so we
  // provide callbacks. (current version of the backend will only allocate one
  // descriptor, future versions will need to allocate more)
  initInfo.SrvDescriptorHeap = g_SrvUavDescriptorHeap.Get();
  initInfo.SrvDescriptorAllocFn =
      [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
         D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle) {
        g_SrvUavDescHeapAlloc.Alloc(outCpuHandle, outGpuHandle);
      };
  initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
                                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    return g_SrvUavDescHeapAlloc.Free(cpuHandle, gpuHandle);
  };
  ImGui_ImplDX12_Init(&initInfo);

  // Root Signature
  {
    // Root parameters
    // Applications should sort entries in the root signature from most frequently changing to least.
    CD3DX12_ROOT_PARAMETER rootParameters[RootParameter::Count] = {};
    rootParameters[RootParameter::PerDrawConstants].InitAsConstants(4, 0);  // b0, fill g-buffer use 4 constants
    // TODO: should use constant buffer... camera matrices, planes, etc quickly add up...
    rootParameters[RootParameter::FrameConstants].InitAsConstants(FrameContext::frameConstantsSize, 1);  // b1
    rootParameters[RootParameter::BuffersDescriptorIndices].InitAsConstants(SizeOfInUint(BuffersDescriptorIndices), 2);  // b2

    // Static sampler
    std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> staticSamplers{};
    staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
    staticSamplers[1].Init(1, D3D12_FILTER_ANISOTROPIC);

    // Root Signature
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(
        RootParameter::Count, rootParameters, staticSamplers.size(), staticSamplers.data(), flags);

    ID3DBlob* signatureBlobPtr;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                  &signatureBlobPtr, nullptr));

    ID3D12RootSignature* rootSignature = nullptr;
    CHECK_HR(g_Device->CreateRootSignature(
        0, signatureBlobPtr->GetBufferPointer(),
        signatureBlobPtr->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    g_RootSignature.Attach(rootSignature);
  }

  // Command signature for DrawMeshCommands
  {
    std::array<D3D12_INDIRECT_ARGUMENT_DESC, 2> argDesc{};
    argDesc[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    argDesc[0].Constant = {
        .RootParameterIndex = RootParameter::PerDrawConstants,
        .DestOffsetIn32BitValues = 0,
        .Num32BitValuesToSet = SizeOfInUint(DrawMeshCommand::constants),
    };
    argDesc[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

    D3D12_COMMAND_SIGNATURE_DESC signatureDesc = {
        .ByteStride = sizeof(DrawMeshCommand),
        .NumArgumentDescs = argDesc.size(),
        .pArgumentDescs = argDesc.data(),
    };

    CHECK_HR(g_Device->CreateCommandSignature(&signatureDesc, g_RootSignature.Get(),
                                              IID_PPV_ARGS(&g_DrawMeshCommandSignature)));
  }

  // Compute skinning root signature
  // TODO: reuse same command signature as above to avoid switching?
  {
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    CD3DX12_ROOT_PARAMETER1 rootParameters[SkinningCSRootParameter::Count] = {};
    rootParameters[SkinningCSRootParameter::BuffersOffsets].InitAsConstants(SizeOfInUint(SkinningPerDispatchConstants), 0);  // b0
    rootParameters[SkinningCSRootParameter::BuffersDescriptorIndices].InitAsConstants(
        SizeOfInUint(SkinningBuffersDescriptorIndices), 1);  // b1

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(SkinningCSRootParameter::Count, rootParameters, 0, nullptr, flags);

    ID3DBlob* signatureBlobPtr;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signatureBlobPtr, nullptr));

    ID3D12RootSignature* rootSignature = nullptr;
    CHECK_HR(g_Device->CreateRootSignature(0, signatureBlobPtr->GetBufferPointer(), signatureBlobPtr->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSignature)));
    g_ComputeRootSignature.Attach(rootSignature);
  }

  // Mesh Shader Pipeline State for static objects
  {
    // Mesh Shader
    auto amplificationShaderBlob = ReadData(GetAssetFullPath(L"MeshletAS.cso").c_str());
    D3D12_SHADER_BYTECODE amplificationShader = {amplificationShaderBlob.data(), amplificationShaderBlob.size()};

    auto meshShaderBlob = ReadData(GetAssetFullPath(L"MeshletMS.cso").c_str());
    D3D12_SHADER_BYTECODE meshShader = {meshShaderBlob.data(),
                                        meshShaderBlob.size()};

    auto pixelShaderBlob = ReadData(GetAssetFullPath(L"MeshletPS.cso").c_str());
    D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob.data(),
                                         pixelShaderBlob.size()};

    D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.AS = amplificationShader;
    psoDesc.MS = meshShader;
    psoDesc.PS = pixelShader;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = VISIBILITY_BUFFER_FORMAT;
    psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.FrontCounterClockwise = TRUE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc = DefaultSampleDesc();

    auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
    streamDesc.pPipelineStateSubobjectStream = &psoStream;
    streamDesc.SizeInBytes = sizeof(psoStream);

    // create the pso
    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreatePipelineState(&streamDesc,
                                           IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::BasicMS].Attach(pipelineStateObject);
  }

  // Compute skinning pipeline
  {
    auto computeShaderBlob = ReadData(GetAssetFullPath(L"SkinningCS.cso").c_str());
    D3D12_SHADER_BYTECODE computeShader = {computeShaderBlob.data(), computeShaderBlob.size()};

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
        .pRootSignature = g_ComputeRootSignature.Get(),
        .CS = computeShader,
    };

    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::SkinningCS].Attach(pipelineStateObject);
  }

  // Compute culling pipeline
  {
    auto computeShaderBlob = ReadData(GetAssetFullPath(L"InstanceCullingCS.cso").c_str());
    D3D12_SHADER_BYTECODE computeShader = {computeShaderBlob.data(), computeShaderBlob.size()};

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
        .pRootSignature = g_RootSignature.Get(),
        .CS = computeShader,
    };

    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::InstanceCullingCS].Attach(pipelineStateObject);
  }

  // Fill G-Buffer pipeline
  {
    auto computeShaderBlob = ReadData(GetAssetFullPath(L"FillGBufferCS.cso").c_str());
    D3D12_SHADER_BYTECODE computeShader = {computeShaderBlob.data(), computeShaderBlob.size()};

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
        .pRootSignature = g_RootSignature.Get(),
        .CS = computeShader,
    };

    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::FillGBufferCS].Attach(pipelineStateObject);
  }

  // Final image composition VS/PS pipeline
  {
    auto vertexShaderBlob = ReadData(GetAssetFullPath(L"FullScreenTriangleVS.cso").c_str());
    D3D12_SHADER_BYTECODE vertexShader = {vertexShaderBlob.data(), vertexShaderBlob.size()};

    auto pixelShaderBlob = ReadData(GetAssetFullPath(L"FinalComposePS.cso").c_str());
    D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob.data(), pixelShaderBlob.size()};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout.NumElements = 0;
    psoDesc.InputLayout.pInputElementDescs = nullptr;
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.VS = vertexShader;
    psoDesc.PS = pixelShader;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT;
    psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc = DefaultSampleDesc();

    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::FinalComposeVS].Attach(pipelineStateObject);
  }

  // MeshStore: TODO: make an instance method / DRY
  {
    // TODO: compute worst case scenario from the scene.
    // wait, everyone has more than 8GB VRAM in 2025, right? right?
    static constexpr size_t numVertices = 5'000'000;
    static constexpr size_t numIndices = 10'000'000;
    static constexpr size_t numPrimitives = 7'000'000;
    static constexpr size_t numInstances = MESH_INSTANCE_COUNT;
    static constexpr size_t numMeshlets = 100'000;
    static constexpr size_t numMaterials = 5000;
    static constexpr size_t numMatrices = 3000;

    D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_GPU_UPLOAD};

    // Positions buffer
    {
      size_t bufSiz = numVertices * sizeof(XMFLOAT3);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
      auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_VertexPositions.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_VertexPositions.SetName(L"Positions Store");
      g_MeshStore.m_VertexPositions.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numVertices),
                                                         .StructureByteStride = sizeof(XMFLOAT3),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                               .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                               .Buffer = {.FirstElement = 0,
                                                          .NumElements = static_cast<UINT>(numVertices),
                                                          .StructureByteStride = sizeof(XMFLOAT3),
                                                          .CounterOffsetInBytes = 0,
                                                          .Flags = D3D12_BUFFER_UAV_FLAG_NONE}};
      g_MeshStore.m_VertexPositions.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_MeshStore.m_VertexPositions.AllocUavDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_VertexPositions.Resource(), &srvDesc,
                                         g_MeshStore.m_VertexPositions.SrvDescriptorHandle());
      g_Device->CreateUnorderedAccessView(g_MeshStore.m_VertexPositions.Resource(), nullptr, &uavDesc,
                                          g_MeshStore.m_VertexPositions.UavDescriptorHandle());
    }

    // Normals buffer
    {
      size_t bufSiz = numVertices * sizeof(XMFLOAT3);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_VertexNormals.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_VertexNormals.SetName(L"Normals Store");
      g_MeshStore.m_VertexNormals.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numVertices),
                                                         .StructureByteStride = sizeof(XMFLOAT3),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_VertexNormals.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_VertexNormals.Resource(), &srvDesc,
                                         g_MeshStore.m_VertexNormals.SrvDescriptorHandle());
      // TODO: we also need a UAV because we need to transform normals during skinning
    }

    // Tangents buffer
    {
      size_t bufSiz = numVertices * sizeof(XMFLOAT4);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_VertexTangents.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_VertexTangents.SetName(L"Tangents Store");
      g_MeshStore.m_VertexTangents.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numVertices),
                                                         .StructureByteStride = sizeof(XMFLOAT4),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_VertexTangents.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_VertexTangents.Resource(), &srvDesc,
                                         g_MeshStore.m_VertexTangents.SrvDescriptorHandle());
      // TODO: we also need a UAV because we need to transform tangents during skinning
    }

    // UVs buffer
    {
      size_t bufSiz = numVertices * sizeof(XMFLOAT2);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_VertexUVs.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_VertexUVs.SetName(L"UVs Store");
      g_MeshStore.m_VertexUVs.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numVertices),
                                                         .StructureByteStride = sizeof(XMFLOAT2),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_VertexUVs.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_VertexUVs.Resource(), &srvDesc,
                                         g_MeshStore.m_VertexUVs.SrvDescriptorHandle());

    }

    // Blend weights/indices buffer
    {
      size_t bufSiz = numVertices * sizeof(XMUINT2);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_VertexBlendWeightsAndIndices.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_VertexBlendWeightsAndIndices.SetName(L"Blend weights/indices Store");
      g_MeshStore.m_VertexBlendWeightsAndIndices.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numVertices),
                                                         .StructureByteStride = sizeof(XMUINT2),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_VertexBlendWeightsAndIndices.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_VertexBlendWeightsAndIndices.Resource(), &srvDesc,
                                         g_MeshStore.m_VertexBlendWeightsAndIndices.SrvDescriptorHandle());
    }

    // Meshlets buffer
    {
      size_t bufSiz = numMeshlets * sizeof(MeshletData);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_Meshlets.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_Meshlets.SetName(L"Meshlets Store");
      g_MeshStore.m_Meshlets.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numMeshlets),
                                                         .StructureByteStride = sizeof(MeshletData),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_Meshlets.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_Meshlets.Resource(), &srvDesc,
                                         g_MeshStore.m_Meshlets.SrvDescriptorHandle());
    }

    // Meshlet unique vertex indices buffer
    {
      size_t bufSiz = numIndices * sizeof(UINT);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_MeshletUniqueIndices.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_MeshletUniqueIndices.SetName(L"Unique vertex indices Store");
      g_MeshStore.m_MeshletUniqueIndices.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numIndices),
                                                         .StructureByteStride = sizeof(UINT),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_MeshletUniqueIndices.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_MeshletUniqueIndices.Resource(), &srvDesc,
                                         g_MeshStore.m_MeshletUniqueIndices.SrvDescriptorHandle());
    }

    // Meshlet primitives buffer (packed 10|10|10|2)
    {
      size_t bufSiz = numPrimitives * sizeof(MeshletTriangle);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_MeshletPrimitives.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_MeshletPrimitives.SetName(L"Primitives Store");
      g_MeshStore.m_MeshletPrimitives.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numPrimitives),
                                                         .StructureByteStride = sizeof(MeshletTriangle),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_MeshletPrimitives.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_MeshletPrimitives.Resource(), &srvDesc,
                                         g_MeshStore.m_MeshletPrimitives.SrvDescriptorHandle());
    }

    // Materials buffer
    {
      size_t bufSiz = numMaterials * sizeof(Material::m_GpuData);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_Materials.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_Materials.SetName(L"Materials Store");
      g_MeshStore.m_Materials.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numMaterials),
                                                         .StructureByteStride = sizeof(Material::m_GpuData),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_Materials.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_Materials.Resource(), &srvDesc,
                                         g_MeshStore.m_Materials.SrvDescriptorHandle());
    }

    // Instances buffer
    {
      size_t bufSiz = numInstances * sizeof(MeshInstance::data);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
        g_MeshStore.m_Instances[i].CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
        g_MeshStore.m_Instances[i].SetName(std::format(L"Instances Store {}", i));
        g_MeshStore.m_Instances[i].Map();

        // descriptor
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                .Buffer = {.FirstElement = 0,
                                                           .NumElements = static_cast<UINT>(numInstances),
                                                           .StructureByteStride = sizeof(MeshInstance::data),
                                                           .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
        g_MeshStore.m_Instances[i].AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
        g_Device->CreateShaderResourceView(g_MeshStore.m_Instances[i].Resource(), &srvDesc,
                                           g_MeshStore.m_Instances[i].SrvDescriptorHandle());
      }
    }

    // Bone Matrices buffer
    {
      size_t bufSiz = numMatrices * sizeof(XMFLOAT4X4);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
        g_MeshStore.m_BoneMatrices[i].CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
        g_MeshStore.m_BoneMatrices[i].SetName(std::format(L"Bone Matrices Store {}", i));
        g_MeshStore.m_BoneMatrices[i].Map();

        // descriptor
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                                .Buffer = {.FirstElement = 0,
                                                           .NumElements = static_cast<UINT>(numMatrices),
                                                           .StructureByteStride = sizeof(XMFLOAT4X4),
                                                           .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
        g_MeshStore.m_BoneMatrices[i].AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
        g_Device->CreateShaderResourceView(g_MeshStore.m_BoneMatrices[i].Resource(), &srvDesc,
                                           g_MeshStore.m_BoneMatrices[i].SrvDescriptorHandle());
      }
    }

    // Draw Meshlets commands
    {
      size_t bufSiz = DRAW_MESH_CMDS_COUNTER_OFFSET + sizeof(UINT);  // counter
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

      g_DrawMeshCommands.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_DrawMeshCommands.SetName(L"Draw Meshlets command buffer");
      g_DrawMeshCommands.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numInstances),
                                                         .StructureByteStride = sizeof(DrawMeshCommand),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                               .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                               .Buffer = {.FirstElement = 0,
                                                          .NumElements = static_cast<UINT>(numInstances),
                                                          .StructureByteStride = sizeof(DrawMeshCommand),
                                                          .CounterOffsetInBytes = DRAW_MESH_CMDS_COUNTER_OFFSET,
                                                          .Flags = D3D12_BUFFER_UAV_FLAG_NONE}};

      g_DrawMeshCommands.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_DrawMeshCommands.Resource(), &srvDesc,
                                         g_DrawMeshCommands.SrvDescriptorHandle());

      g_DrawMeshCommands.AllocUavDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateUnorderedAccessView(g_DrawMeshCommands.Resource(), g_DrawMeshCommands.Resource(), &uavDesc,
                                          g_DrawMeshCommands.UavDescriptorHandle());
    }

    // Buffer containg just a UINT (0) used to reset UAV counter.
    {
      size_t bufSiz = sizeof(UINT);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);
      g_UAVCounterReset.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_UAVCounterReset.SetName(L"UAV Reset counter");

      g_UAVCounterReset.Map();
      g_UAVCounterReset.Clear(bufSiz);
      g_UAVCounterReset.Unmap();
    }
  }

  // Vis Buffer output
  {
    D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_DEFAULT};

    D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(VISIBILITY_BUFFER_FORMAT, g_Width, g_Height);
    textureDesc.MipLevels = 1;
    textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear{.Format = textureDesc.Format, .Color = {0.0f, 0.0f, 0.0f, 0.0f}};
    g_VisibilityBuffer.CreateResource(g_Allocator.Get(), &allocDesc, &textureDesc, D3D12_RESOURCE_STATE_COMMON, &clear);
    g_VisibilityBuffer.SetName(L"Visibility Buffer");

    // descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = textureDesc.Format,
                                            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                            .Texture2D = {
                                                .MipLevels = textureDesc.MipLevels,
                                            }};
    g_VisibilityBuffer.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
    g_Device->CreateShaderResourceView(g_VisibilityBuffer.Resource(), &srvDesc,
                                       g_VisibilityBuffer.SrvDescriptorHandle());

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{
        .Format = textureDesc.Format,
        .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
        .Texture2D = {},
    };
    g_VisibilityBuffer.AllocRtvDescriptor(g_RtvDescHeapAlloc);
    g_Device->CreateRenderTargetView(g_VisibilityBuffer.Resource(), nullptr, g_VisibilityBuffer.RtvDescriptorHandle());

  }

  // G-Buffer output
  {
    D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_DEFAULT};

    auto InitGBuffer = [&](auto& buffer, DXGI_FORMAT format, const WCHAR* name) {
      D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, g_Width, g_Height);
      textureDesc.MipLevels = 1;
      textureDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

      buffer.CreateResource(g_Allocator.Get(), &allocDesc, &textureDesc);
      buffer.SetName(name);

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = format,
                                              .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Texture2D = {.MipLevels = textureDesc.MipLevels}};
      D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
          .Format = format, .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D, .Texture2D = {}};

      buffer.AllocSrvDescriptorWithGpuHandle(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(buffer.Resource(), &srvDesc, buffer.SrvDescriptorHandle());

      buffer.AllocUavDescriptorWithGpuHandle(g_SrvUavDescHeapAlloc);
      g_Device->CreateUnorderedAccessView(buffer.Resource(), nullptr, &uavDesc, buffer.UavDescriptorHandle());

      buffer.AllocUavReadWriteDescriptor(g_UavReadWriteDescHeapAlloc);
      g_Device->CreateUnorderedAccessView(buffer.Resource(), nullptr, &uavDesc, buffer.UavReadWriteDescriptorHandle());
    };

    InitGBuffer(g_GBuffer.worldPosition, GBUFFER_WORLD_POSITION_FORMAT, L"G-Buffer world position");
    InitGBuffer(g_GBuffer.worldNormal, GBUFFER_WORLD_NORMAL_FORMAT, L"G-Buffer world normal");
    InitGBuffer(g_GBuffer.baseColor, GBUFFER_BASE_COLOR_FORMAT, L"G-Buffer base color");
  }

  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    auto ctx = &g_FrameContext[i];

    ctx->buffersDescriptorsIndices = g_MeshStore.BuffersDescriptorIndices(i);
    ctx->skinningBuffersDescriptorsIndices = g_MeshStore.SkinningBuffersDescriptorIndices(i);
    ctx->cullingBuffersDescriptorsIndices = {
        .InstancesBufferId = g_MeshStore.InstancesBufferId(i),
        .DrawMeshCommandsBufferId = g_DrawMeshCommands.UavDescriptorIndex(),
    };
  }

  // timestamp readback buffer
  {
    size_t bufSize = sizeof(UINT64) * Timestamp::Count;

    D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);

    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      g_FrameContext[i].timestampReadBackBuffer.CreateResource(g_Allocator.Get(), &allocDesc, &readbackDesc);
      g_FrameContext[i].timestampReadBackBuffer.SetName(std::format(L"Timestamp Readback Buffer {}", i));
    }
  }
}

static void MoveToNextFrame()
{
  auto ctx = &g_FrameContext[g_FrameIndex];
  CHECK_HR(g_CommandQueue->Signal(ctx->fence.Get(), ctx->fenceValue));

  g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
  auto nextCtx = &g_FrameContext[g_FrameIndex];

  // If the next frame is not ready to be rendered yet, wait until it is ready
  WaitForFrame(nextCtx);

  // Set the fence value for the next frame
  nextCtx->fenceValue++;
}

// wait until gpu is finished with command list
static void WaitForFrame(FrameContext* ctx)
{
  // if the current fence value is still less than "fenceValue", then we
  // know the GPU has not finished executing the command queue since it has
  // not reached the "g_CommandQueue->Signal(fence, fenceValue)" command
  if (ctx->fence->GetCompletedValue() < ctx->fenceValue) {
    // we have the fence create an event which is signaled once the
    // fence's current value is "fenceValue"
    CHECK_HR(ctx->fence->SetEventOnCompletion(ctx->fenceValue, g_FenceEvent));

    // We will wait until the fence has triggered the event that it's
    // current value has reached "fenceValue". once it's value has reached
    // "fenceValue", we know the command queue has finished executing
    WaitForSingleObject(g_FenceEvent, INFINITE);
  }
}

static void WaitGPUIdle()
{
  auto ctx = &g_FrameContext[g_FrameIndex];

  CHECK_HR(g_CommandQueue->Signal(ctx->fence.Get(), ctx->fenceValue));

  CHECK_HR(ctx->fence->SetEventOnCompletion(ctx->fenceValue, g_FenceEvent));
  WaitForSingleObject(g_FenceEvent, INFINITE);
  ctx->fenceValue++;
}

// Helper function for resolving the full path of assets.
static std::wstring GetAssetFullPath(LPCWSTR assetName)
{
  return g_AssetsPath + assetName;
}

static void PrintAdapterInformation(IDXGIAdapter1* adapter)
{
  wprintf(L"DXGI_ADAPTER_DESC1:\n");
  wprintf(L"    Description = %s\n", g_AdapterDesc.Description);
  wprintf(L"    VendorId = 0x%X (%s)\n", g_AdapterDesc.VendorId,
          VendorIDToStr(g_AdapterDesc.VendorId));
  wprintf(L"    DeviceId = 0x%X\n", g_AdapterDesc.DeviceId);
  wprintf(L"    SubSysId = 0x%X\n", g_AdapterDesc.SubSysId);
  wprintf(L"    Revision = 0x%X\n", g_AdapterDesc.Revision);
  wprintf(L"    DedicatedVideoMemory = %zu B (%s)\n",
          g_AdapterDesc.DedicatedVideoMemory,
          SizeToStr(g_AdapterDesc.DedicatedVideoMemory).c_str());
  wprintf(L"    DedicatedSystemMemory = %zu B (%s)\n",
          g_AdapterDesc.DedicatedSystemMemory,
          SizeToStr(g_AdapterDesc.DedicatedSystemMemory).c_str());
  wprintf(L"    SharedSystemMemory = %zu B (%s)\n",
          g_AdapterDesc.SharedSystemMemory,
          SizeToStr(g_AdapterDesc.SharedSystemMemory).c_str());

  const D3D12_FEATURE_DATA_D3D12_OPTIONS& options =
      g_Allocator->GetD3D12Options();
  wprintf(L"D3D12_FEATURE_DATA_D3D12_OPTIONS:\n");
  wprintf(L"    StandardSwizzle64KBSupported = %u\n",
          options.StandardSwizzle64KBSupported ? 1 : 0);
  wprintf(L"    CrossAdapterRowMajorTextureSupported = %u\n",
          options.CrossAdapterRowMajorTextureSupported ? 1 : 0);

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

  if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)))) {
    wprintf(L"DXGI_QUERY_VIDEO_MEMORY_INFO:\n");

    for (UINT groupIndex = 0; groupIndex < 2; ++groupIndex) {
      const DXGI_MEMORY_SEGMENT_GROUP group =
          groupIndex == 0 ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL
                          : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
      const wchar_t* const groupName =
          groupIndex == 0 ? L"DXGI_MEMORY_SEGMENT_GROUP_LOCAL"
                          : L"DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL";
      DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
      CHECK_HR(adapter3->QueryVideoMemoryInfo(0, group, &info));

      wprintf(L"    %s:\n", groupName);
      wprintf(L"        Budget = %llu B (%s)\n", info.Budget,
              SizeToStr(info.Budget).c_str());
      wprintf(L"        CurrentUsage = %llu B (%s)\n", info.CurrentUsage,
              SizeToStr(info.CurrentUsage).c_str());
      wprintf(L"        AvailableForReservation = %llu B (%s)\n",
              info.AvailableForReservation,
              SizeToStr(info.AvailableForReservation).c_str());
      wprintf(L"        CurrentReservation = %llu B (%s)\n",
              info.CurrentReservation,
              SizeToStr(info.CurrentReservation).c_str());
    }
  }

  assert(g_Device);
  D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1 = {};

  if (SUCCEEDED(g_Device->CheckFeatureSupport(
          D3D12_FEATURE_ARCHITECTURE1, &architecture1, sizeof architecture1))) {
    wprintf(L"D3D12_FEATURE_DATA_ARCHITECTURE1:\n");
    wprintf(L"    UMA: %u\n", architecture1.UMA ? 1 : 0);
    wprintf(L"    CacheCoherentUMA: %u\n",
            architecture1.CacheCoherentUMA ? 1 : 0);
    wprintf(L"    IsolatedMMU: %u\n", architecture1.IsolatedMMU ? 1 : 0);
  }
}

// TODO: rewrite this mess
static std::shared_ptr<MeshInstance> LoadMesh3D(std::shared_ptr<Mesh3D> mesh)
{
  auto meshBasePath = std::filesystem::path(mesh->name).parent_path();

  // Create MeshInstance
  auto mi = std::make_shared<MeshInstance>();
  mi->instanceBufferOffset = g_MeshStore.ReserveInstance(sizeof(MeshInstance::data));

  // Assign it to the meshlets of this instance
  std::vector<MeshletData> instanceMeshlets = mesh->meshlets;
  for (auto& m : instanceMeshlets) {
    m.instanceIndex = mi->instanceBufferOffset / sizeof(MeshInstance::data);
  }

  {
    auto it = g_Scene.meshInstanceMap.find(mesh->name);
    if (it == std::end(g_Scene.meshInstanceMap)) {
      // CreateGeometry
      // vertex data
      if (mesh->Skinned()) {
        // in case of skinned mesh, these will be filled by a compute shader
        mi->data.firstPosition = g_MeshStore.ReservePositions(mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstNormal = g_MeshStore.ReserveNormals(mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstTangent = g_MeshStore.ReserveTangents(mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);

        auto smi = std::make_shared<SkinnedMeshInstance>();
        smi->offsets.basePositionsBuffer =
            g_MeshStore.CopyPositions(mesh->positions.data(), mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        smi->offsets.baseNormalsBuffer =
            g_MeshStore.CopyNormals(mesh->normals.data(), mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        smi->offsets.baseTangentsBuffer =
            g_MeshStore.CopyTangents(mesh->tangents.data(), mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);
        smi->offsets.blendWeightsAndIndicesBuffer =
            g_MeshStore.CopyBWI(mesh->blendWeightsAndIndices.data(), mesh->BlendWeightsAndIndicesBufferSize()) /
            sizeof(XMUINT2);

        smi->numVertices = mesh->header.numVerts;
        smi->numBoneMatrices = mesh->SkinMatricesSize();
        // TODO: should reuse bone matrice buffer for meshes of same model which share skin
        smi->offsets.boneMatricesBuffer =
            g_MeshStore.ReserveBoneMatrices(mesh->SkinMatricesBufferSize()) / sizeof(XMFLOAT4X4);

        smi->meshInstance = mi;
        mi->skinnedMeshInstance = smi;

        g_Scene.skinnedMeshInstances.push_back(smi);
        g_Scene.numBoneMatrices += smi->numBoneMatrices;
      } else {
        mi->data.firstPosition =
            g_MeshStore.CopyPositions(mesh->positions.data(), mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstNormal =
            g_MeshStore.CopyNormals(mesh->normals.data(), mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstTangent =
            g_MeshStore.CopyTangents(mesh->tangents.data(), mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);
      }

      mi->data.firstUV = g_MeshStore.CopyUVs(mesh->uvs.data(), mesh->UvsBufferSize()) / sizeof(XMFLOAT2);

      // meshlet data
      mi->data.firstMeshlet =
          g_MeshStore.CopyMeshlets(instanceMeshlets.data(), mesh->MeshletBufferSize()) / sizeof(MeshletData);
      mi->data.firstVertIndex =
          g_MeshStore.CopyMeshletUniqueIndices(mesh->uniqueVertexIndices.data(), mesh->MeshletIndexBufferSize()) /
          sizeof(UINT);
      mi->data.firstPrimitive =
          g_MeshStore.CopyMeshletPrimitives(mesh->primitiveIndices.data(), mesh->MeshletPrimitiveBufferSize()) /
          sizeof(UINT);
    } else {
      auto i = it->second[0];
      mi->data.firstPosition = i->data.firstPosition;
      mi->data.firstNormal = i->data.firstNormal;
      mi->data.firstTangent = i->data.firstTangent;
      mi->data.firstUV = i->data.firstUV;

      mi->data.firstMeshlet =
          g_MeshStore.CopyMeshlets(instanceMeshlets.data(), mesh->MeshletBufferSize()) / sizeof(MeshletData);
      mi->data.firstVertIndex = i->data.firstVertIndex;
      mi->data.firstPrimitive = i->data.firstPrimitive;

      if (mesh->Skinned()) {
        // these will be filled by compute shader so we need new ones.
        mi->data.firstPosition = g_MeshStore.ReservePositions(mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstNormal = g_MeshStore.ReserveNormals(mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstTangent = g_MeshStore.ReserveTangents(mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);

        auto smi = std::make_shared<SkinnedMeshInstance>();
        smi->offsets = i->skinnedMeshInstance->offsets;

        smi->numVertices = mesh->header.numVerts;
        smi->numBoneMatrices = i->skinnedMeshInstance->numBoneMatrices;
        // TODO: should reuse bone matrice buffer for meshes of same model which share skin
        smi->offsets.boneMatricesBuffer =
            g_MeshStore.ReserveBoneMatrices(smi->BoneMatricesBufferSize()) / sizeof(XMFLOAT4X4);

        smi->meshInstance = mi;
        mi->skinnedMeshInstance = smi;

        g_Scene.skinnedMeshInstances.push_back(smi);
        g_Scene.numBoneMatrices += smi->numBoneMatrices;
      }
    }
  }

  mi->data.numMeshlets = mesh->meshlets.size();
  mi->mesh = mesh;
  // We update MeshIntance buffer everyframe so don't need to copy data here.

  g_Scene.meshInstanceMap[mesh->name].push_back(mi);
  g_Scene.numMeshInstances++;

  return mi;
}

static UINT CreateTexture(std::filesystem::path filename)
{
  auto it = g_Textures.find(filename);
  if (it != g_Textures.end()) return it->second->SrvDescriptorIndex();

  auto tex = std::make_shared<GpuBuffer>(); // unique_ptr?

  TexMetadata metadata;
  ScratchImage image;

  LoadFromDDSFile(filename.wstring().c_str(), DDS_FLAGS_NONE, &metadata, image);

  // TODO: handle multiple mips level + handle 3d textures
  // ref: DirectXTex\DirectXTexD3D12.cpp#PrepareUpload
  D3D12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(metadata.format, metadata.width, metadata.height,
                                                                 metadata.arraySize, metadata.mipLevels);

  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_GPU_UPLOAD};
  tex->CreateResource(g_Allocator.Get(), &allocDesc, &textureDesc);
  tex->MapOpaque();

  std::vector<D3D12_SUBRESOURCE_DATA> subresources;
  HRESULT hr = PrepareUpload(g_Device.Get(), image.GetImages(), image.GetImageCount(),
                                      metadata, subresources);

  tex->Copy(subresources.data(), static_cast<UINT>(subresources.size()), 0);

  tex->Unmap();

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = textureDesc.Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;

  tex->AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
  tex->SetName(L"Texture: " + filename.wstring() + L" " + std::to_wstring(tex->SrvDescriptorIndex()));
  g_Device->CreateShaderResourceView(tex->Resource(), &srvDesc, tex->SrvDescriptorHandle());

  g_Textures[filename] = tex;

  return tex->SrvDescriptorIndex();
}
}  // namespace Renderer
