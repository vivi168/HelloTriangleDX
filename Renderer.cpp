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
static constexpr UINT PRESENT_SYNC_INTERVAL = 0;
static constexpr UINT NUM_DESCRIPTORS_PER_HEAP = 1024;

static constexpr DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
static constexpr D3D_FEATURE_LEVEL FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1;

// ========== Enums

enum class PSO { BasicMS, SkinningCS };

namespace RootParameter
{
enum Slots : size_t { PerMeshConstants = 0, FrameConstants, BuffersDescriptorIndices, Count };
}

namespace SkinningCSRootParameter
{
enum Slots : size_t { BuffersOffsets = 0, BuffersDescriptorIndices, Count };
}

// ========== Structs

struct VisibleMeshlet {
  UINT meshletIndex;
  UINT primitiveIndex;
};

struct SkinnedMeshInstance;

struct MeshInstance {
  struct {
    // TODO: keep this separate to minimize data transfer?
    struct {
      XMFLOAT4X4 WorldViewProj;
      XMFLOAT4X4 WorldMatrix;
      XMFLOAT4X4 NormalMatrix;
    } matrices; // updated each frame

    struct {
      UINT positionsBuffer;
      UINT normalsBuffer;
      // TODO: tangents
      UINT uvsBuffer;

      UINT meshletsBuffer;
      UINT uniqueIndicesBuffer;
      UINT primitivesBuffer;
      UINT pad[2];
    } offsets;
  } data;  // upload this struct on the GPU in MeshInstanceBuffer

  UINT instanceBufferOffset;
  UINT numMeshlets;
  std::shared_ptr<SkinnedMeshInstance> skinnedMeshInstance = nullptr; // not null if skinned mesh
  std::shared_ptr<Mesh3D> mesh = nullptr;
};

// only used for compute shader skinning pass
struct SkinnedMeshInstance {
  struct {
    UINT basePositionsBuffer;
    UINT baseNormalsBuffer;
    UINT blendWeightsAndIndicesBuffer;
    UINT boneMatricesBuffer;
  } offsets;

  UINT numVertices;
  UINT numBoneMatrices;
  std::shared_ptr<MeshInstance> meshInstance = nullptr;  // should never be null

  size_t BoneMatricesBufferSize() const { return sizeof(XMFLOAT4X4) * numBoneMatrices; }

  static constexpr size_t NumOffsets = 7;

  std::array<UINT, NumOffsets> BuffersOffsets() const
  {
    assert(meshInstance);
    return {
        offsets.basePositionsBuffer,
        meshInstance->data.offsets.positionsBuffer,
        offsets.baseNormalsBuffer,
        meshInstance->data.offsets.normalsBuffer,
        offsets.blendWeightsAndIndicesBuffer,
        offsets.boneMatricesBuffer,
        numVertices,
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
  std::vector<std::shared_ptr<SkinnedMeshInstance>> skinnedMeshInstances;

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
    m_HeapStartGpu = m_Heap->GetGPUDescriptorHandleForHeapStart();
    m_HeapHandleIncrement =
        device->GetDescriptorHandleIncrementSize(m_HeapType);

    for (UINT i = 0; i < desc.NumDescriptors; i++) m_FreeIndices.push_back(i);
  }

  void Destroy()
  {
    m_Heap = nullptr;
    m_FreeIndices.clear();
  }

  void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescHandle,
             D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescHandle)
  {
    assert(m_FreeIndices.size() > 0);

    UINT idx = m_FreeIndices.front();
    m_FreeIndices.pop_front();

    outCpuDescHandle->ptr = m_HeapStartCpu.ptr + (idx * m_HeapHandleIncrement);
    outGpuDescHandle->ptr = m_HeapStartGpu.ptr + (idx * m_HeapHandleIncrement);
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

struct HeapDescriptor
{
  UINT index;
  D3D12_CPU_DESCRIPTOR_HANDLE handle;

  void Alloc(DescriptorHeapListAllocator& allocator)
  {
    index = allocator.Alloc(&handle);
  }
};

struct GpuBuffer {
  ID3D12Resource* Resource() const { return resource.Get(); };

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(size_t offset = 0) const { return resource->GetGPUVirtualAddress() + offset; }

  void AllocSrvDescriptor(DescriptorHeapListAllocator& allocator) { srvDescriptor.Alloc(allocator); }

  void AllocUavDescriptor(DescriptorHeapListAllocator& allocator) { uavDescriptor.Alloc(allocator); }

  UINT SrvDescriptorIndex() const { return srvDescriptor.index; }

  UINT UavDescriptorIndex() const { return uavDescriptor.index; }

  D3D12_CPU_DESCRIPTOR_HANDLE SrvDescriptorHandle() const { return srvDescriptor.handle; }

  D3D12_CPU_DESCRIPTOR_HANDLE UavDescriptorHandle() const { return uavDescriptor.handle; }

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

  void MapOpaque() { CHECK_HR(resource->Map(0, &EMPTY_RANGE, nullptr)); }

  void Unmap() { resource->Unmap(0, nullptr); }

  void Copy(size_t offset, const void* data, size_t size) { memcpy((BYTE*)address + offset, data, size); }

  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT DstSubresource = 0)
  {
    resource->WriteToSubresource(DstSubresource, nullptr, data->pData, data->RowPitch, data->SlicePitch);
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
    allocation->Release();
    allocation = nullptr;
  }

private:
  ComPtr<ID3D12Resource> resource;
  HeapDescriptor srvDescriptor;
  HeapDescriptor uavDescriptor;
  D3D12MA::Allocation* allocation;
  void* address;
  D3D12_RESOURCE_STATES currentState;
  std::wstring resourceName;
};

struct Material {
  struct {
    UINT baseColorId;
    UINT metallicRoughnessId;
    UINT normalMapId;
    UINT pad;
  } m_GpuData;

  UINT m_MaterialBufferOffset;

  UINT MaterialIndex() const { return m_MaterialBufferOffset / sizeof(m_GpuData); }
};

struct FrameContext {
  struct {
    float time;
    float deltaTime;
  } frameConstants;

  static constexpr size_t frameConstantsSize = sizeof(frameConstants) / sizeof(UINT32);

  ComPtr<ID3D12Resource> renderTarget;

  ComPtr<ID3D12CommandAllocator> commandAllocator;
  ComPtr<ID3D12CommandAllocator> computeCommandAllocator;
  ComPtr<ID3D12Fence> fence;
  ComPtr<ID3D12Fence> computeFence;
  UINT64 fenceValue;

  void Reset() {
    renderTarget.Reset();
    commandAllocator.Reset();
    computeCommandAllocator.Reset();
    fence.Reset();
    computeFence.Reset();
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

  // TODO: Tangents

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

  UINT CopyInstance(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.instancesBuffer;
    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      m_Instances[i].Copy(offset, data, size);
    }
    m_CurrentOffsets.instancesBuffer += size;

    return offset;
  }

  void UpdateInstance(const void* data, size_t size, UINT offset, UINT frameIndex)
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

  static constexpr size_t NumDescriptorIndices = 9;
  static constexpr size_t NumSkinningCSDescriptorIndices = 4;

  // TODO: struct for these 2? enum?
  std::array<UINT, NumDescriptorIndices> BuffersDescriptorIndices(UINT frameIndex) const
  {
    return {
        m_VertexPositions.SrvDescriptorIndex(),
        m_VertexNormals.SrvDescriptorIndex(),
        // TODO: tangents
        m_VertexUVs.SrvDescriptorIndex(),

        m_Meshlets.SrvDescriptorIndex(),
        m_VisibleMeshlets.SrvDescriptorIndex(),
        m_MeshletUniqueIndices.SrvDescriptorIndex(),
        m_MeshletPrimitives.SrvDescriptorIndex(),
        m_Materials.SrvDescriptorIndex(),
        m_Instances[frameIndex].SrvDescriptorIndex(),
    };
  }

  std::array<UINT, NumSkinningCSDescriptorIndices> ComputeBuffersDescriptorIndices(UINT frameIndex) const
  {
    return {
        m_VertexPositions.UavDescriptorIndex(),
        m_VertexNormals.UavDescriptorIndex(),
        m_VertexBlendWeightsAndIndices.SrvDescriptorIndex(),
        m_BoneMatrices[frameIndex].SrvDescriptorIndex(),
    };
  }

  GpuBuffer m_VertexPositions;
  GpuBuffer m_VertexNormals;
  // TODO: GpuBuffer m_VertexTangents;
  GpuBuffer m_VertexUVs;
  GpuBuffer m_VertexBlendWeightsAndIndices;

  GpuBuffer m_Meshlets;
  GpuBuffer m_VisibleMeshlets;  // written by compute shader
  GpuBuffer m_MeshletUniqueIndices;
  GpuBuffer m_MeshletPrimitives;

  GpuBuffer m_Materials;
  GpuBuffer m_Instances[FRAME_BUFFER_COUNT];  // updated by CPU

  GpuBuffer m_BoneMatrices[FRAME_BUFFER_COUNT];  // updated by CPU

  GpuBuffer m_MeshletCullCommands[FRAME_BUFFER_COUNT];
  GpuBuffer m_DrawMeshletCommands[FRAME_BUFFER_COUNT];

  struct {
    // vertex data
    UINT positionsBuffer = 0;
    UINT normalsBuffer = 0;
    // TODO: UINT tangentsBuffer = 0;
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
static ComPtr<ID3D12CommandQueue> g_ComputeCommandQueue;
static ComPtr<ID3D12GraphicsCommandList6> g_CommandList;
static ComPtr<ID3D12GraphicsCommandList6> g_ComputeCommandList;

static FrameContext g_FrameContext[FRAME_BUFFER_COUNT];
static UINT g_FrameIndex;
static HANDLE g_FenceEvent;

// Resources
static ComPtr<ID3D12DescriptorHeap> g_SrvUavDescriptorHeap;
static DescriptorHeapListAllocator g_SrvUavDescHeapAlloc;

static ComPtr<ID3D12DescriptorHeap> g_RtvDescriptorHeap;
static UINT g_RtvDescriptorSize;

static GpuBuffer g_DepthStencilBuffer;
static ComPtr<ID3D12DescriptorHeap> g_DepthStencilDescriptorHeap;

// PSO
static std::unordered_map<PSO, ComPtr<ID3D12PipelineState>> g_PipelineStateObjects;
static ComPtr<ID3D12RootSignature> g_RootSignature;
static ComPtr<ID3D12RootSignature> g_ComputeRootSignature;

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

  // Allocate instances in a contiguous manner
  for (const auto& [k, instances] : g_Scene.meshInstanceMap) {
    for (auto mi : instances) {
      mi->instanceBufferOffset = g_MeshStore.CopyInstance(&mi->data, sizeof(mi->data));
    }
  }
}

void Update(float time, float dt)
{
  auto ctx = &g_FrameContext[g_FrameIndex];

  // Per frame root constants
  {
    ctx->frameConstants.time = time;
    ctx->frameConstants.deltaTime = dt;
  }

  // Per object constant buffer
  {
    const XMMATRIX projection = XMMatrixPerspectiveFovRH(45.f * (XM_PI / 180.f), g_AspectRatio, 0.1f, 1000.f);

    XMMATRIX view = g_Scene.camera->LookAt();
    XMMATRIX viewProjection = view * projection;

    // TODO: here compute planes for frustum culling

    for (auto& node : g_Scene.nodes) {
      auto model = node.model;

      if (model->HasCurrentAnimation()) {
        size_t i = 0;
        for (auto &[k, skin] : model->skins) {
          std::vector<XMFLOAT4X4> matrices = model->currentAnimation.BoneTransforms(dt, skin.get());

          for (auto &smi : node.skinnedMeshInstances) {
            // TODO: should reuse bone matrice buffer for meshes of same model which share skin
            // TODO: should buffer these updates and do only one memcpy...
            g_MeshStore.UpdateBoneMatrices(matrices.data(), sizeof(XMFLOAT4X4) * matrices.size(),
                                           smi->offsets.boneMatricesBuffer * sizeof(XMFLOAT4X4), g_FrameIndex);
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

        XMStoreFloat4x4(&mi->data.matrices.WorldViewProj, XMMatrixTranspose(worldViewProjection));
        XMStoreFloat4x4(&mi->data.matrices.WorldMatrix, XMMatrixTranspose(world));
        XMStoreFloat4x4(&mi->data.matrices.NormalMatrix, normalMatrix);

        // TODO: should buffer these updates and do only one memcpy...
        g_MeshStore.UpdateInstance(&mi->data, sizeof(mi->data), mi->instanceBufferOffset, g_FrameIndex);
      }
    }
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
}

// Add a PSO helper struct ?
void Render()
{
  auto ctx = &g_FrameContext[g_FrameIndex];

  // we can only reset an allocator once the gpu is done with it. Resetting an
  // allocator frees the memory that the command list was stored in
  CHECK_HR(ctx->commandAllocator->Reset());
  CHECK_HR(ctx->computeCommandAllocator->Reset());

  // reset the command list. by resetting the command list we are putting it
  // into a recording state so we can start recording commands into the command
  // allocator. The command allocator that we reference here may have multiple
  // command lists associated with it, but only one can be recording at any
  // time. Make sure that any other command lists associated to this command
  // allocator are in the closed state (not recording).
  CHECK_HR(g_CommandList->Reset(ctx->commandAllocator.Get(), g_PipelineStateObjects[PSO::BasicMS].Get()));
  CHECK_HR(g_ComputeCommandList->Reset(ctx->computeCommandAllocator.Get(), g_PipelineStateObjects[PSO::SkinningCS].Get()));

  // here we start recording commands into the g_CommandList (which all the
  // commands will be stored in the g_CommandAllocators)

  // transition the "g_FrameIndex" render target from the present state to the
  // render target state so the command list draws to it starting from here
  auto presentToRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
      ctx->renderTarget.Get(), D3D12_RESOURCE_STATE_PRESENT,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  g_CommandList->ResourceBarrier(1, &presentToRenderTargetBarrier);

  // here we again get the handle to our current render target view so we can
  // set it as the render target in the output merger stage of the pipeline
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {
      g_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr +
      g_FrameIndex * g_RtvDescriptorSize};
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
      g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

  // set the render target for the output merger stage (the output of the
  // pipeline)
  g_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

  g_CommandList->ClearDepthStencilView(
      g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
      D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  // Clear the render target by using the ClearRenderTargetView command
  if (g_Raster) {
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  } else {
    const float clearColor[] = {0.6f, 0.8f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  }

  std::array descriptorHeaps{g_SrvUavDescriptorHeap.Get()};
  g_CommandList->SetDescriptorHeaps(descriptorHeaps.size(), descriptorHeaps.data());

  g_CommandList->SetGraphicsRootSignature(g_RootSignature.Get());

  g_CommandList->SetGraphicsRoot32BitConstants(RootParameter::FrameConstants, FrameContext::frameConstantsSize,
                                               &ctx->frameConstants, 0);
  auto h = g_MeshStore.BuffersDescriptorIndices(g_FrameIndex);
  g_CommandList->SetGraphicsRoot32BitConstants(RootParameter::BuffersDescriptorIndices, MeshStore::NumDescriptorIndices,
                                               h.data(), 0);

  D3D12_VIEWPORT viewport{0.f, 0.f, (float)g_Width, (float)g_Height, 0.f, 1.f};
  g_CommandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissorRect{0, 0, g_Width, g_Height};
  g_CommandList->RSSetScissorRects(1, &scissorRect);

  // record skinning compute commands if needed
  if (g_Scene.skinnedMeshInstances.size() > 0) {
    std::array descriptorHeaps{g_SrvUavDescriptorHeap.Get()};
    g_ComputeCommandList->SetDescriptorHeaps(descriptorHeaps.size(), descriptorHeaps.data());

    g_ComputeCommandList->SetComputeRootSignature(g_ComputeRootSignature.Get());

    auto h = g_MeshStore.ComputeBuffersDescriptorIndices(g_FrameIndex);
    g_ComputeCommandList->SetComputeRoot32BitConstants(SkinningCSRootParameter::BuffersDescriptorIndices,
                                                MeshStore::NumSkinningCSDescriptorIndices, h.data(), 0);
    auto b0 = g_MeshStore.m_VertexPositions.Transition(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    g_ComputeCommandList->ResourceBarrier(1, &b0);

    for (auto smi : g_Scene.skinnedMeshInstances) {
      auto o = smi->BuffersOffsets();
      g_ComputeCommandList->SetComputeRoot32BitConstants(SkinningCSRootParameter::BuffersOffsets,
                                                  SkinnedMeshInstance::NumOffsets, o.data(), 0);

      g_ComputeCommandList->Dispatch(DivRoundUp(smi->numVertices, 64), 1, 1);
    }
    auto b1 = g_MeshStore.m_VertexPositions.Transition(D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    g_ComputeCommandList->ResourceBarrier(1, &b1);
  }
  CHECK_HR(g_ComputeCommandList->Close());

  // record draw calls
  for (const auto& [k, instances] : g_Scene.meshInstanceMap) {
    auto mi = instances[0];
    struct {
      UINT firstInstanceIndex;
      UINT numMeshlets;
      UINT numInstances;
    } ronre = {mi->instanceBufferOffset / sizeof(MeshInstance::data), mi->numMeshlets, instances.size()};
    g_CommandList->SetGraphicsRoot32BitConstants(RootParameter::PerMeshConstants, sizeof(ronre) / sizeof(UINT), &ronre, 0);
    g_CommandList->DispatchMesh(mi->numMeshlets, instances.size(), 1);
  }

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_CommandList.Get());

  // transition the "g_FrameIndex" render target from the render target state to
  // the present state. If the debug layer is enabled, you will receive a
  // warning if present is called on the render target when it's not in the
  // present state
  auto postRenderBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
      ctx->renderTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

  g_CommandList->ResourceBarrier(1, &postRenderBarrier);

  CHECK_HR(g_CommandList->Close());

  // ==========

  // execute skinning command list if needed
  if (g_Scene.skinnedMeshInstances.size() > 0) {
    std::array ppCommandLists{static_cast<ID3D12CommandList*>(g_ComputeCommandList.Get())};
    g_ComputeCommandQueue->ExecuteCommandLists(ppCommandLists.size(), ppCommandLists.data());

    CHECK_HR(g_ComputeCommandQueue->Signal(ctx->computeFence.Get(), ctx->fenceValue));
    CHECK_HR(g_CommandQueue->Wait(ctx->computeFence.Get(), ctx->fenceValue));
  }

  // execute graphic command list
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

    // TODO: tangents

    g_MeshStore.m_VertexUVs.Unmap();
    g_MeshStore.m_VertexUVs.Reset();

    g_MeshStore.m_VertexBlendWeightsAndIndices.Unmap();
    g_MeshStore.m_VertexBlendWeightsAndIndices.Reset();

    g_MeshStore.m_Meshlets.Unmap();
    g_MeshStore.m_Meshlets.Reset();

    g_MeshStore.m_VisibleMeshlets.Unmap();
    g_MeshStore.m_VisibleMeshlets.Reset();

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
  g_RootSignature.Reset();

  CloseHandle(g_FenceEvent);
  g_CommandList.Reset();
  g_ComputeCommandList.Reset();
  g_CommandQueue.Reset();
  g_ComputeCommandQueue.Reset();

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
    CHECK_HR(
        g_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)));
    g_CommandQueue.Attach(commandQueue);
  }

  // Create Compute Command Queue
  {
    D3D12_COMMAND_QUEUE_DESC cqDesc{
        .Type = D3D12_COMMAND_LIST_TYPE_COMPUTE,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
    };

    ID3D12CommandQueue* commandQueue = nullptr;
    CHECK_HR(g_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)));
    g_ComputeCommandQueue.Attach(commandQueue);
  }

  // Create Command Allocator
  {
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      {
        ID3D12CommandAllocator* commandAllocator = nullptr;
        CHECK_HR(g_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
        g_FrameContext[i].commandAllocator.Attach(commandAllocator);
      }

      // Compute
      {
        ID3D12CommandAllocator* commandAllocator = nullptr;
        CHECK_HR(g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&commandAllocator)));
        g_FrameContext[i].computeCommandAllocator.Attach(commandAllocator);
      }
    }

    // create the command list with the first allocator
    CHECK_HR(
        g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    g_FrameContext[0].commandAllocator.Get(),
                                    NULL, IID_PPV_ARGS(&g_CommandList)));

    // Compute
    CHECK_HR(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                         g_FrameContext[0].computeCommandAllocator.Get(), NULL,
                                         IID_PPV_ARGS(&g_ComputeCommandList)));

    // command lists are created in the recording state. our main loop will set
    // it up for recording again so close it now
    g_CommandList->Close();
    g_ComputeCommandList->Close();
  }

  // Create Synchronization objects
  {
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      {
        ID3D12Fence* fence = nullptr;
        CHECK_HR(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                       IID_PPV_ARGS(&fence)));
        g_FrameContext[i].fence.Attach(fence);
      }
      // Compute
      {
        ID3D12Fence* fence = nullptr;
        CHECK_HR(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        g_FrameContext[i].computeFence.Attach(fence);
      }

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
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_BUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    // This heap will not be directly referenced by the shaders (not shader
    // visible), as this will store the output from the pipeline otherwise we
    // would set the heap's flag to D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
    CHECK_HR(g_Device->CreateDescriptorHeap(&rtvHeapDesc,
                                            IID_PPV_ARGS(&rtvDescriptorHeap)));
    g_RtvDescriptorHeap.Attach(rtvDescriptorHeap);

    // get the size of a descriptor in this heap (this is a rtv heap, so only
    // rtv descriptors should be stored in it. Descriptor sizes may vary from
    // g_Device to g_Device, which is why there is no set size and we must ask
    // the g_Device to give us the size. we will use this size to increment a
    // descriptor handle offset
    g_RtvDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // get a handle to the first descriptor in the descriptor heap. a handle is
    // basically a pointer, but we cannot literally use it like a c++ pointer.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{
        g_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()};

    // Create a RTV for each buffer (double buffering is two buffers, tripple
    // buffering is 3).
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      // first we get the n'th buffer in the swap chain and store it in the n'th
      // position of our ID3D12Resource array
      ID3D12Resource* res = nullptr;
      CHECK_HR(g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&res)));
      g_FrameContext[i].renderTarget.Attach(res);

      // then we "create" a render target view which binds the swap chain buffer
      // (ID3D12Resource[n]) to the rtv handle
      g_Device->CreateRenderTargetView(g_FrameContext[i].renderTarget.Get(),
                                       nullptr, rtvHandle);

      // we increment the rtv handle by the rtv descriptor size we got above
      rtvHandle.ptr += g_RtvDescriptorSize;
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
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = NUM_DESCRIPTORS_PER_HEAP;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  CHECK_HR(g_Device->CreateDescriptorHeap(&heapDesc,
                                          IID_PPV_ARGS(&g_SrvUavDescriptorHeap)));

  g_SrvUavDescHeapAlloc.Create(g_Device.Get(), g_SrvUavDescriptorHeap.Get());

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
        return g_SrvUavDescHeapAlloc.Alloc(outCpuHandle, outGpuHandle);
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
    rootParameters[RootParameter::PerMeshConstants].InitAsConstants(3, 0);  // b0
    rootParameters[RootParameter::FrameConstants].InitAsConstants(FrameContext::frameConstantsSize, 1);  // b1
    rootParameters[RootParameter::BuffersDescriptorIndices].InitAsConstants(MeshStore::NumDescriptorIndices, 2);  // b2

    // Static sampler
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);

    // Root Signature
    D3D12_ROOT_SIGNATURE_FLAGS flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(
        RootParameter::Count, rootParameters, 1, staticSamplers, flags);

    ID3DBlob* signatureBlobPtr;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                  &signatureBlobPtr, nullptr));

    ID3D12RootSignature* rootSignature = nullptr;
    CHECK_HR(g_Device->CreateRootSignature(
        0, signatureBlobPtr->GetBufferPointer(),
        signatureBlobPtr->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    g_RootSignature.Attach(rootSignature);
  }

  // Compute skinning root signature
  {
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    CD3DX12_ROOT_PARAMETER1 rootParameters[SkinningCSRootParameter::Count] = {};
    rootParameters[SkinningCSRootParameter::BuffersOffsets].InitAsConstants(SkinnedMeshInstance::NumOffsets, 0);  // b0
    rootParameters[SkinningCSRootParameter::BuffersDescriptorIndices].InitAsConstants(
        MeshStore::NumSkinningCSDescriptorIndices, 1);  // b1

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
    auto meshShaderBlob = ReadData(GetAssetFullPath(L"MeshletMS.cso").c_str());
    D3D12_SHADER_BYTECODE meshShader = {meshShaderBlob.data(),
                                        meshShaderBlob.size()};

    auto pixelShaderBlob = ReadData(GetAssetFullPath(L"MeshletPS.cso").c_str());
    D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob.data(),
                                         pixelShaderBlob.size()};

    D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.MS = meshShader;
    psoDesc.PS = pixelShader;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT;
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

  // TODO: we need a PSO for the 2nd pass (lighting pass) that will use the G buffer to compute the final image

  // MeshStore: TODO: make an instance method / DRY
  {
    // TODO: better estimate
    static constexpr size_t numVertices = 2'000'000;
    static constexpr size_t numMeshlets = 50'000;
    static constexpr size_t numIndices = 10'000'000;
    static constexpr size_t numPrimitives = 3'000'000;
    static constexpr size_t numInstances = 10000;
    static constexpr size_t numMaterials = 1000;
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

    }

    // Tangents buffer
    {
      // TODO...
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

    // Visible meshlets buffer
    {
      size_t bufSiz = numMeshlets * sizeof(VisibleMeshlet);
      auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSiz);

      g_MeshStore.m_VisibleMeshlets.CreateResource(g_Allocator.Get(), &allocDesc, &bufferDesc);
      g_MeshStore.m_VisibleMeshlets.SetName(L"Visible Meshlets Store");
      g_MeshStore.m_VisibleMeshlets.Map();

      // descriptor
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                              .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                              .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                              .Buffer = {.FirstElement = 0,
                                                         .NumElements = static_cast<UINT>(numMeshlets),
                                                         .StructureByteStride = sizeof(VisibleMeshlet),
                                                         .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};
      g_MeshStore.m_VisibleMeshlets.AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
      g_Device->CreateShaderResourceView(g_MeshStore.m_VisibleMeshlets.Resource(), &srvDesc,
                                         g_MeshStore.m_VisibleMeshlets.SrvDescriptorHandle());
      // TODO: UAV as well (for writing)
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

static std::shared_ptr<MeshInstance> LoadMesh3D(std::shared_ptr<Mesh3D> mesh)
{
  auto meshBasePath = std::filesystem::path(mesh->name).parent_path();

  // Create MeshInstance
  auto mi = std::make_shared<MeshInstance>();
  {
    auto it = g_Scene.meshInstanceMap.find(mesh->name);
    if (it == std::end(g_Scene.meshInstanceMap)) {
      // CreateGeometry
      // vertex data
      if (mesh->Skinned()) {
        // in case of skinned mesh, these will be filled by a compute shader
        mi->data.offsets.positionsBuffer = g_MeshStore.ReservePositions(mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.offsets.normalsBuffer = g_MeshStore.ReserveNormals(mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);

        auto smi = std::make_shared<SkinnedMeshInstance>();
        smi->offsets.basePositionsBuffer =
            g_MeshStore.CopyPositions(mesh->positions.data(), mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        smi->offsets.baseNormalsBuffer =
            g_MeshStore.CopyNormals(mesh->normals.data(), mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        smi->offsets.blendWeightsAndIndicesBuffer =
            g_MeshStore.CopyBWI(mesh->blendWeightsAndIndices.data(), mesh->BlendWeightsAndIndicesBufferSize()) / sizeof(XMUINT2);

        smi->numVertices = mesh->header.numVerts;
        smi->numBoneMatrices = mesh->SkinMatricesSize();
        // TODO: should reuse bone matrice buffer for meshes of same model which share skin
        smi->offsets.boneMatricesBuffer = g_MeshStore.ReserveBoneMatrices(mesh->SkinMatricesBufferSize()) / sizeof(XMFLOAT4X4);

        smi->meshInstance = mi;
        mi->skinnedMeshInstance = smi;

        g_Scene.skinnedMeshInstances.push_back(smi);
      } else {
        mi->data.offsets.positionsBuffer =
            g_MeshStore.CopyPositions(mesh->positions.data(), mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.offsets.normalsBuffer =
            g_MeshStore.CopyNormals(mesh->normals.data(), mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
      }
      mi->data.offsets.uvsBuffer = g_MeshStore.CopyUVs(mesh->uvs.data(), mesh->UvsBufferSize()) / sizeof(XMFLOAT2);

      // meshlet data
      mi->data.offsets.meshletsBuffer =
          g_MeshStore.CopyMeshlets(mesh->meshlets.data(), mesh->MeshletBufferSize()) / sizeof(MeshletData);
      mi->data.offsets.uniqueIndicesBuffer =
          g_MeshStore.CopyMeshletUniqueIndices(mesh->uniqueVertexIndices.data(), mesh->MeshletIndexBufferSize()) /
          sizeof(UINT);
      mi->data.offsets.primitivesBuffer =
          g_MeshStore.CopyMeshletPrimitives(mesh->primitiveIndices.data(), mesh->MeshletPrimitiveBufferSize()) /
          sizeof(UINT);
    } else {
      auto i = it->second[0];
      mi->data.offsets = i->data.offsets;

      if (mesh->Skinned()) {
        // these will be filled by compute shader so we need new ones.
        mi->data.offsets.positionsBuffer = g_MeshStore.ReservePositions(mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.offsets.normalsBuffer = g_MeshStore.ReserveNormals(mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);

        auto smi = std::make_shared<SkinnedMeshInstance>();
        smi->offsets = i->skinnedMeshInstance->offsets;

        smi->numVertices = mesh->header.numVerts;
        smi->numBoneMatrices = i->skinnedMeshInstance->numBoneMatrices;
        // TODO: should reuse bone matrice buffer for meshes of same model which share skin
        smi->offsets.boneMatricesBuffer = g_MeshStore.ReserveBoneMatrices(smi->BoneMatricesBufferSize()) / sizeof(XMFLOAT4X4);

        smi->meshInstance = mi;
        mi->skinnedMeshInstance = smi;

        g_Scene.skinnedMeshInstances.push_back(smi);
      }
    }
  }

  mi->numMeshlets = mesh->meshlets.size();
  mi->mesh = mesh;
  g_Scene.meshInstanceMap[mesh->name].push_back(mi);

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
  tex->SetName(L"Texture: " + filename.wstring());
  tex->MapOpaque();

  const Image* img = image.GetImage(0, 0, 0);

  D3D12_SUBRESOURCE_DATA textureSubresourceData = {};
  textureSubresourceData.pData = img->pixels;
  textureSubresourceData.RowPitch = img->rowPitch;
  textureSubresourceData.SlicePitch = img->slicePitch;

  tex->Copy(&textureSubresourceData);

  tex->Unmap();

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = textureDesc.Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;

  tex->AllocSrvDescriptor(g_SrvUavDescHeapAlloc);
  g_Device->CreateShaderResourceView(tex->Resource(), &srvDesc, tex->SrvDescriptorHandle());

  g_Textures[filename] = tex;

  return tex->SrvDescriptorIndex();
}
}  // namespace Renderer
