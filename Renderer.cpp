#include "stdafx.h"

#include "Renderer.h"
#include "RendererHelper.h"

#include "shaders/Shared.h"

#include "Win32Application.h"

#include "Camera.h"
#include "Mesh.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Renderer
{
// ========== Constants

static constexpr size_t FRAME_BUFFER_COUNT = 3;
static constexpr size_t MESH_INSTANCE_COUNT = 10'000;
static constexpr UINT NUM_DESCRIPTORS_PER_HEAP = 16384;

static constexpr DXGI_FORMAT VISIBILITY_BUFFER_FORMAT = DXGI_FORMAT_R32_UINT;
static constexpr DXGI_FORMAT SHADOW_BUFFER_FORMAT = DXGI_FORMAT_R8_UNORM;
static constexpr DXGI_FORMAT GBUFFER_WORLD_POSITION_FORMAT = DXGI_FORMAT_R32G32B32A32_FLOAT;
static constexpr DXGI_FORMAT GBUFFER_WORLD_NORMAL_FORMAT = DXGI_FORMAT_R10G10B10A2_UNORM;
static constexpr DXGI_FORMAT GBUFFER_BASE_COLOR_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT GBUFFER_METALLIC_ROUGHNESS_FORMAT = DXGI_FORMAT_R8G8_UNORM;
static constexpr DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static constexpr DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;

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
  TotalBegin = 0,
  TotalEnd,
  SkinBegin,
  SkinEnd,
  CullBegin,
  CullEnd,
  DrawBegin,
  DrawEnd,
  FillGBufferBegin,
  FillGBufferEnd,
  ShadowsBegin,
  ShadowsEnd,
  FinalComposeBegin,
  FinalComposeEnd,
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

// TODO: RHI implementation
struct AccelerationStructure {
  std::shared_ptr<IssouRHI::Buffer> resultData;
  std::shared_ptr<IssouRHI::Buffer> scratch;

  // FIXME: quick TMP hack, create a AccelerationStructure class on rhi side
  void AllocBuffers(size_t resultDataSize, size_t scratchSize, IssouRHI::Device* device)
  {
    IssouRHI::BufferDesc scratchDesc{
      .label = "Acceleration structure Scratch Resource",
      .size = scratchSize,
      .usage = IssouRHI::BufferUsage::Storage,
    };
    scratch = device->CreateBuffer(scratchDesc);

    IssouRHI::BufferDesc resultDesc{
      .label = "Acceleration structure Result Resource",
      .size = resultDataSize,
      .usage = IssouRHI::BufferUsage::Storage | IssouRHI::BufferUsage::RayTracingAccelerationStructure,
    };
    resultData = device->CreateBuffer(resultDesc);
  }

  // FIXME: quick TMP hack, create a AccelerationStructure class on rhi side
  UINT ResultDataSrvDescriptorIndex(IssouRHI::Device* device) {
    if (srv) return srv.index;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE,
                                            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                            .RaytracingAccelerationStructure = {.Location = resultData->GpuAddress()}};

    srv = device->AllocSrvUavDescriptor();
    device->GetNativeDevice()->CreateShaderResourceView(nullptr, &srvDesc, srv.cpuHandle);

    return srv.index;
  }

  void Reset()
  {
    resultData.reset();
    scratch.reset();
  }

  IssouRHI::DescriptorAllocation srv;
};

struct SkinnedMeshInstance;

struct MeshInstance {
  MeshInstanceData data;

  UINT instanceBufferOffset;
  UINT indexBufferOffset;
  UINT rtInstanceOffset;
  D3D12_GPU_VIRTUAL_ADDRESS blasBufferAddress = 0;

  std::weak_ptr<SkinnedMeshInstance> skinnedMeshInstance;
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

  // TODO: should we move these to mesh store
  // (as well as raytracing specifics below)
  // and instead of g_MeshStore, have scene.meshStore ?
  std::unordered_map<std::wstring, std::vector<std::shared_ptr<MeshInstance>>> meshInstanceMap;
  UINT numMeshInstances = 0;
  std::vector<std::shared_ptr<SkinnedMeshInstance>> skinnedMeshInstances;
  UINT numBoneMatrices = 0;

  // RayTracing specific
  // the following contains only first mesh instance of each mesh
  // (unless skinned, in which case all corresponding mesh instances are added)
  std::vector<std::shared_ptr<MeshInstance>> uniqueMeshInstances;

  std::vector<AccelerationStructure> blasBuffers;
  AccelerationStructure tlasBuffer;
  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> rtInstanceDescriptors;
  std::shared_ptr<IssouRHI::Buffer> rtInstanceDescBuffer;

  Camera* camera;
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

  std::shared_ptr<IssouRHI::Buffer> timestampReadBackBuffer;
  ComPtr<ID3D12CommandAllocator> commandAllocator;

  void Reset() {
    timestampReadBackBuffer.reset();
    commandAllocator.Reset();
  }
};

struct MeshStore {
  // Vertex data
  UINT WritePositions(const void* data, size_t size)
  {
    // TODO: should ensure it is mapped
    UINT offset = m_CurrentOffsets.positionsBuffer;
    m_VertexPositions->Write({offset, size}, data);
    m_CurrentOffsets.positionsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT ReservePositions(size_t size)
  {
    UINT offset = m_CurrentOffsets.positionsBuffer;
    m_CurrentOffsets.positionsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT WriteNormals(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.normalsBuffer;
    m_VertexNormals->Write({offset, size}, data);
    m_CurrentOffsets.normalsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT ReserveNormals(size_t size)
  {
    UINT offset = m_CurrentOffsets.normalsBuffer;
    m_CurrentOffsets.normalsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT WriteTangents(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.tangentsBuffer;
    m_VertexTangents->Write({offset, size}, data);
    m_CurrentOffsets.tangentsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT ReserveTangents(size_t size)
  {
    UINT offset = m_CurrentOffsets.tangentsBuffer;
    m_CurrentOffsets.tangentsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT WriteUVs(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.uvsBuffer;
    m_VertexUVs->Write({offset, size}, data);
    m_CurrentOffsets.uvsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT WriteBWI(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.bwiBuffer;
    m_VertexBlendWeightsAndIndices->Write({offset, size}, data);
    m_CurrentOffsets.bwiBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT WriteIndices(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.indexBuffer;
    m_VertexIndices->Write({offset, size}, data);
    m_CurrentOffsets.indexBuffer += static_cast<UINT>(size);

    return offset;
  }

  // Meshlet data

  UINT WriteMeshlets(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.meshletsBuffer;
    m_Meshlets->Write({offset, size}, data);
    m_CurrentOffsets.meshletsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT WriteMeshletUniqueIndices(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.uniqueIndicesBuffer;
    m_MeshletUniqueIndices->Write({offset, size}, data);
    m_CurrentOffsets.uniqueIndicesBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT WriteMeshletPrimitives(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.primitivesBuffer;
    m_MeshletPrimitives->Write({offset, size}, data);
    m_CurrentOffsets.primitivesBuffer += static_cast<UINT>(size);

    return offset;
  }

  // meta data

  UINT WriteMaterial(const void* data, size_t size)
  {
    UINT offset = m_CurrentOffsets.materialsBuffer;
    m_Materials->Write({offset, size}, data);
    m_CurrentOffsets.materialsBuffer += static_cast<UINT>(size);

    return offset;
  }

  UINT ReserveInstance(size_t size)
  {
    UINT offset = m_CurrentOffsets.instancesBuffer;
    m_CurrentOffsets.instancesBuffer += static_cast<UINT>(size);

    return offset;
  }

  void UpdateInstances(const void* data, size_t size, UINT offset, UINT frameIndex)
  {
    m_Instances[frameIndex]->Write({offset, size}, data);
  }

  UINT ReserveBoneMatrices(size_t size)
  {
    UINT offset = m_CurrentOffsets.boneMatricesBuffer;
    m_CurrentOffsets.boneMatricesBuffer += static_cast<UINT>(size);

    return offset;
  }

  void UpdateBoneMatrices(const void* data, size_t size, UINT offset, UINT frameIndex)
  {
    m_BoneMatrices[frameIndex]->Write({offset, size}, data);
  }

  // TODO: this won't be necessary here once we have bindGroups / pass descriptor
  BuffersDescriptorIndices BuffersDescriptorIndices(UINT frameIndex) const
  {
    return {
        .vertexPositionsBufferId = m_VertexPositions->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT3)).index,
        .vertexNormalsBufferId = m_VertexNormals->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT3)).index,
        .vertexTangentsBufferId = m_VertexTangents->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT4)).index,
        .vertexUVsBufferId = m_VertexUVs->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT2)).index,

        .meshletsBufferId = m_Meshlets->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(MeshletData)).index,
        .meshletVertIndicesBufferId = m_MeshletUniqueIndices->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(UINT)).index,
        .meshletsPrimitivesBufferId = m_MeshletPrimitives->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(MeshletTriangle)).index,

        .materialsBufferId = m_Materials->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(Material::m_GpuData)).index,
        .instancesBufferId = m_Instances[frameIndex]->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(MeshInstance::data)).index,
    };
  }

  // TODO: this won't be necessary here once we have bindGroups / pass descriptor
  SkinningBuffersDescriptorIndices SkinningBuffersDescriptorIndices(UINT frameIndex) const
  {
    return {
        .vertexPositionsBufferId = m_VertexPositions->UavDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT3)).index,
        .vertexNormalsBufferId = m_VertexNormals->UavDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT3)).index,
        .vertexTangentsBufferId = m_VertexTangents->UavDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT4)).index,
        .vertexBlendWeightsAndIndicesBufferId = m_VertexBlendWeightsAndIndices->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMUINT2)).index,
        .boneMatricesBufferId = m_BoneMatrices[frameIndex]->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(XMFLOAT4X4)).index,
    };
  }

  // TODO: this won't be necessary here once we have bindGroups / pass descriptor
  UINT InstancesBufferId(UINT frameIndex) const
  {
    return m_Instances[frameIndex]->SrvDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(MeshInstance::data)).index;
  }

  void Init(IssouRHI::Device* device)
  {
    // TODO: compute worst case scenario from the scene.
    // wait, everyone has more than 8GB VRAM in 2026, right? right?
    static constexpr size_t numVertices = 5'000'000;
    static constexpr size_t numIndices = 10'000'000;
    static constexpr size_t numPrimitives = 7'000'000;
    static constexpr size_t numInstances = MESH_INSTANCE_COUNT;
    static constexpr size_t numMeshlets = 100'000;
    static constexpr size_t numMaterials = 5000;
    static constexpr size_t numMatrices = 3000;

    // Positions buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "Positions Store",
        .size = numVertices * sizeof(XMFLOAT3),
        .usage = IssouRHI::BufferUsage::MapWrite | IssouRHI::BufferUsage::Storage,
      };
      m_VertexPositions = device->CreateBuffer(desc);
    }

    // Normals buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "Normals Store",
        .size = numVertices * sizeof(XMFLOAT3),
        .usage = IssouRHI::BufferUsage::MapWrite | IssouRHI::BufferUsage::Storage,
      };
      m_VertexNormals = device->CreateBuffer(desc);
    }

    // Tangents buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "Tangents Store",
        .size = numVertices * sizeof(XMFLOAT4),
        .usage = IssouRHI::BufferUsage::MapWrite | IssouRHI::BufferUsage::Storage,
      };
      m_VertexTangents = device->CreateBuffer(desc);
    }

    // UVs buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "UVs Store",
        .size = numVertices * sizeof(XMFLOAT2),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_VertexUVs = device->CreateBuffer(desc);
    }

    // Blend weights/indices buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "Blend weights/indices Store",
        .size = numVertices * sizeof(XMUINT2),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_VertexBlendWeightsAndIndices = device->CreateBuffer(desc);
    }

    // Vertex indices
    {
      IssouRHI::BufferDesc desc{
        .label = "Vertex indices Store",
        .size = numIndices * sizeof(UINT),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_VertexIndices = device->CreateBuffer(desc);
    }

    // Meshlets buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "Meshlets buffer",
        .size = numMeshlets * sizeof(MeshletData),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_Meshlets = device->CreateBuffer(desc);
    }

    // Meshlet unique vertex indices buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "Meshlets indices buffer",
        .size = numIndices * sizeof(UINT),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_MeshletUniqueIndices = device->CreateBuffer(desc);
    }

    // Meshlet primitives buffer (packed 10|10|10|2)
    {
      IssouRHI::BufferDesc desc{
        .label = "Primitives Store",
        .size = numPrimitives * sizeof(MeshletTriangle),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_MeshletPrimitives = device->CreateBuffer(desc);
    }

    // Materials buffer
    {
      IssouRHI::BufferDesc desc{
        .label = "Materials Store",
        .size = numMaterials * sizeof(Material::m_GpuData),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_Materials = device->CreateBuffer(desc);
    }

    // Instances buffer
    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      IssouRHI::BufferDesc desc{
        .label = std::format("Instances Store {}", i),
        .size = numInstances * sizeof(MeshInstance::data),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_Instances[i] = device->CreateBuffer(desc);
    }

    // Bone Matrices buffer
    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      IssouRHI::BufferDesc desc{
        .label = std::format("Bone Matrices Store {}", i),
        .size = numMatrices * sizeof(XMFLOAT4X4),
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      m_BoneMatrices[i] = device->CreateBuffer(desc);
    }
  }

  std::shared_ptr<IssouRHI::Buffer> m_VertexPositions;
  std::shared_ptr<IssouRHI::Buffer> m_VertexNormals;
  std::shared_ptr<IssouRHI::Buffer> m_VertexTangents;
  std::shared_ptr<IssouRHI::Buffer> m_VertexUVs;
  std::shared_ptr<IssouRHI::Buffer> m_VertexBlendWeightsAndIndices;

  std::shared_ptr<IssouRHI::Buffer> m_VertexIndices; // needed for BLAS

  std::shared_ptr<IssouRHI::Buffer> m_Meshlets;
  std::shared_ptr<IssouRHI::Buffer> m_MeshletUniqueIndices;
  std::shared_ptr<IssouRHI::Buffer> m_MeshletPrimitives;

  std::shared_ptr<IssouRHI::Buffer> m_Materials;
  std::shared_ptr<IssouRHI::Buffer> m_Instances[FRAME_BUFFER_COUNT];  // updated by CPU

  std::shared_ptr<IssouRHI::Buffer> m_BoneMatrices[FRAME_BUFFER_COUNT];  // updated by CPU

  struct {
    // vertex data
    UINT positionsBuffer = 0;
    UINT normalsBuffer = 0;
    UINT tangentsBuffer = 0;
    UINT uvsBuffer = 0;
    UINT bwiBuffer = 0;

    UINT indexBuffer = 0;

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
static std::wstring GetAssetFullPath(LPCWSTR assetName);
static std::shared_ptr<MeshInstance> LoadMesh3D(std::shared_ptr<Mesh3D> mesh);
static UINT CreateTexture(std::filesystem::path filename);

// ========== Global variables

static UINT g_Width;
static UINT g_Height;
static float g_AspectRatio;
static bool g_EnableRTShadows = true;
static float g_SunTime = 0.5f;

static std::wstring g_Title;
static std::wstring g_AssetsPath;

// Pipeline objects
static ID3D12Device5* g_Device;
static D3D12MA::Allocator* g_Allocator;
static IssouRHI::Device* g_RhiDevice;
static IssouRHI::Surface* g_Surface;

// swapchain used to switch between render targets
// container for command lists
static ID3D12CommandQueue* g_CommandQueue;
static ComPtr<ID3D12GraphicsCommandList8> g_CommandList;

static FrameContext g_FrameContext[FRAME_BUFFER_COUNT];

// Resources
static std::shared_ptr<IssouRHI::Texture> g_DepthStencilBuffer;
// TODO: RHI class for this
static ComPtr<ID3D12QueryHeap> g_TimestampQueryHeap;

// PSO
static std::unordered_map<PSO, ComPtr<ID3D12PipelineState>> g_PipelineStateObjects;
static ComPtr<ID3D12RootSignature> g_RootSignature;
static ComPtr<ID3D12RootSignature> g_ComputeRootSignature;
static ComPtr<ID3D12CommandSignature> g_DrawMeshCommandSignature;

static ComPtr<ID3D12StateObject> g_DxrStateObject;
static std::shared_ptr<IssouRHI::Buffer> g_RayGenShaderTable;
static std::shared_ptr<IssouRHI::Buffer> g_MissShaderTable;
static std::shared_ptr<IssouRHI::Buffer> g_HitGroupShaderTable;

static std::shared_ptr<IssouRHI::Buffer> g_DrawMeshCommands;      // written by compute shader
static std::shared_ptr<IssouRHI::Buffer> g_UAVCounterReset;

static std::shared_ptr<IssouRHI::Texture> g_VisibilityBuffer;
static std::shared_ptr<IssouRHI::Texture> g_ShadowBuffer;

struct GBuffer {
  std::shared_ptr<IssouRHI::Texture> worldPosition;
  std::shared_ptr<IssouRHI::Texture> worldNormal;
  std::shared_ptr<IssouRHI::Texture> baseColor;

  FillGBufferPerDispatchConstants PerDispatchConstants(UINT visBufferDescId)
  {
    return {
        .VisibilityBufferId = visBufferDescId,
        .WorldPositionId = worldPosition->CreateView()->UavDescriptorAlloc().index,
        .WorldNormalId = worldNormal->CreateView()->UavDescriptorAlloc().index,
        .BaseColorId = baseColor->CreateView()->UavDescriptorAlloc().index,
    };
  }

  void Reset()
  {
    worldPosition.reset();
    worldNormal.reset();
    baseColor.reset();
  }
};

static GBuffer g_GBuffer;

static MeshStore g_MeshStore;
static std::unordered_map<std::wstring, std::shared_ptr<Material>> g_MaterialMap;
static std::unordered_map<std::wstring, std::shared_ptr<IssouRHI::Texture>> g_Textures;
static Scene g_Scene;

static std::atomic<size_t> g_CpuAllocationCount{0};

// ========== Public functions

void InitWindow(UINT width, UINT height, std::wstring name)
{
  g_Width = width;
  g_Height = height;
  g_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
  g_Title = name;
}

void Init(std::shared_ptr<IssouRHI::Device> device, std::shared_ptr<IssouRHI::Surface> surface)
{
  g_RhiDevice = device.get();  // FIXME: TMP raw ptr!
  g_Surface = surface.get(); // FIXME: TMP raw ptr!
  g_Device = device->GetNativeDevice();
  g_Allocator = device->GetAllocator();
  g_CommandQueue = device->GetNativeQueue();

  InitD3D();
  InitFrameResources();
}

void LoadAssets()
{
  for (auto &node : g_Scene.nodes) {
    for (auto &mesh : node.model->meshes) {
      auto mi = LoadMesh3D(mesh);

      node.meshInstances.push_back(mi);
      if (auto smi = mi->skinnedMeshInstance.lock()) {
        node.skinnedMeshInstances.push_back(smi);
      }
    }
  }

  // RayTracing acceleration structures setup
  ComPtr<ID3D12CommandAllocator> commandAllocator;
  ComPtr<ID3D12GraphicsCommandList4> commandList;
  ComPtr<ID3D12Fence> fence;
  UINT64 fenceValue = 1;
  HANDLE fenceEvent = nullptr;

  {
    CHECK_HR(g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
    CHECK_HR(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), NULL,
                                         IID_PPV_ARGS(&commandList)));
    // Command lists are created in the recording state; close until needed.
    CHECK_HR(commandList->Close());

    CHECK_HR(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent);
  }

  auto waitForGpu = [&]() {
    // Signal and increment the fence value.
    UINT64 fenceToWaitFor = fenceValue;
    CHECK_HR(g_CommandQueue->Signal(fence.Get(), fenceToWaitFor));
    fenceValue++;

    // Wait until the fence is completed.
    CHECK_HR(fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent));
    WaitForSingleObject(fenceEvent, INFINITE);
  };

  // BLAS creation
  {
    const size_t numMeshes = g_Scene.uniqueMeshInstances.size();

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries(numMeshes);
    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> bottomLevelInputs(numMeshes);

    g_Scene.blasBuffers.resize(numMeshes);

    for (size_t i = 0; i < numMeshes; i++) {
      auto& mi = g_Scene.uniqueMeshInstances[i];
      auto& mesh = mi->mesh;

      // TODO: loop subsets
      geometries[i] = {
          .Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
          .Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,  // TODO: flag none for anyhit?
          .Triangles = {
              .Transform3x4 = 0,
              .IndexFormat = DXGI_FORMAT_R32_UINT,
              .VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
              .IndexCount = mesh->header.numIndices,
              .VertexCount = mesh->header.numVerts,
              .IndexBuffer = g_MeshStore.m_VertexIndices->GpuAddress() + mi->indexBufferOffset,
              .VertexBuffer = {
                  .StartAddress = g_MeshStore.m_VertexPositions->GpuAddress() + mi->data.firstPosition * sizeof(XMFLOAT3),
                  .StrideInBytes = sizeof(XMFLOAT3),
              }}};

      bottomLevelInputs[i] = {.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
                              .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
                              .NumDescs = 1,
                              .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
                              .pGeometryDescs = &geometries[i]};

      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizeInfo{};
      g_Device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs[i], &sizeInfo);
      assert(sizeInfo.ResultDataMaxSizeInBytes > 0);

      g_Scene.blasBuffers[i].AllocBuffers(sizeInfo.ResultDataMaxSizeInBytes, sizeInfo.ScratchDataSizeInBytes, g_RhiDevice);

      // assign blas buffer address to each instances of this mesh
      for (auto& inst : g_Scene.meshInstanceMap[mesh->name]) {
        inst->blasBufferAddress = g_Scene.blasBuffers[i].resultData->GpuAddress();
      }
    }

    CHECK_HR(commandAllocator->Reset());
    CHECK_HR(commandList->Reset(commandAllocator.Get(), NULL));

    for (size_t i = 0; i < numMeshes; i++) {
      D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{
          .DestAccelerationStructureData = g_Scene.blasBuffers[i].resultData->GpuAddress(),
          .Inputs = bottomLevelInputs[i],
          .ScratchAccelerationStructureData = g_Scene.blasBuffers[i].scratch->GpuAddress(),
      };

      commandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
    }

    commandList->Close();
    std::array ppCommandLists{static_cast<ID3D12CommandList*>(commandList.Get())};
    g_CommandQueue->ExecuteCommandLists(static_cast<UINT>(ppCommandLists.size()), ppCommandLists.data());
    waitForGpu();
  }

  // Fill rtInstanceBuffer
  {
    g_Scene.rtInstanceDescriptors.reserve(g_Scene.numMeshInstances);

    for (auto& node : g_Scene.nodes) {
      auto model = node.model;
      XMMATRIX modelMat = model->WorldMatrix();

      for (auto& mi : node.meshInstances) {
        if (mi->mesh->Skinned()) continue;

        XMMATRIX world = mi->mesh->LocalTransformMatrix() * modelMat;

        D3D12_RAYTRACING_INSTANCE_DESC desc{};
        XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(desc.Transform), world);
        // desc.InstanceID;
        desc.InstanceMask = 0xff;
        // desc.InstanceContributionToHitGroupIndex;
        // desc.Flags;
        desc.AccelerationStructure = mi->blasBufferAddress;
        assert(desc.AccelerationStructure != 0);

        g_Scene.rtInstanceDescriptors.push_back(desc);
      }
    }
  }

  // RT instance descriptors buffer
  {
    IssouRHI::BufferDesc desc{
      .label = "RT Instance Desc Buffer",
      .size = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * g_Scene.rtInstanceDescriptors.size(),
      .usage = IssouRHI::BufferUsage::MapWrite,
    };
    g_Scene.rtInstanceDescBuffer = g_RhiDevice->CreateBuffer(desc);
    g_Scene.rtInstanceDescBuffer->Write(IssouRHI::FullBufferRange, g_Scene.rtInstanceDescriptors.data());
  }

  // TLAS creation
  {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS topLevelInputs = {
        .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
        .Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
        .NumDescs = static_cast<UINT>(g_Scene.rtInstanceDescriptors.size()),
        .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
        .InstanceDescs = g_Scene.rtInstanceDescBuffer->GpuAddress(),
    };

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO sizeInfo{};
    g_Device->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &sizeInfo);
    assert(sizeInfo.ResultDataMaxSizeInBytes > 0);

    // Allocate buffer for scene tlas
    g_Scene.tlasBuffer.AllocBuffers(sizeInfo.ResultDataMaxSizeInBytes, sizeInfo.ScratchDataSizeInBytes, g_RhiDevice);

    CHECK_HR(commandAllocator->Reset());
    CHECK_HR(commandList->Reset(commandAllocator.Get(), NULL));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{
        .DestAccelerationStructureData = g_Scene.tlasBuffer.resultData->GpuAddress(),
        .Inputs = topLevelInputs,
        .ScratchAccelerationStructureData = g_Scene.tlasBuffer.scratch->GpuAddress(),
    };

    commandList->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);

    commandList->Close();
    std::array ppCommandLists{static_cast<ID3D12CommandList*>(commandList.Get())};
    g_CommandQueue->ExecuteCommandLists(static_cast<UINT>(ppCommandLists.size()), ppCommandLists.data());
    waitForGpu();
  }
}

static void Update(FrameContext* ctx, float time)
{
  // Per frame root constants
  {
    ctx->frameConstants.Time = time;
    ctx->frameConstants.CameraWS = g_Scene.camera->WorldPos();
    ctx->frameConstants.ScreenSize = {static_cast<float>(g_Width), static_cast<float>(g_Height)};
    ctx->frameConstants.TwoOverScreenSize = {2.0f / static_cast<float>(g_Width), 2.0f / static_cast<float>(g_Height)};
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
        for (auto &[k, skin] : model->skins) {
          std::vector<XMFLOAT4X4> matrices = model->currentAnimation.BoneTransforms(time, skin.get());

          for (auto &smi : node.skinnedMeshInstances) {
            // TODO: should reuse bone matrice buffer for meshes of same model which share skin
            // TODO: should also update the collision data...
            std::copy(matrices.begin(), matrices.end(), tmpBoneMatrices.begin() + smi->offsets.boneMatricesBuffer);
          }
        }
      }  // else identity matrices ?

      XMMATRIX modelMat = model->WorldMatrix();

      for (auto mi : node.meshInstances) {
        XMMATRIX world;

        if (model->HasCurrentAnimation() && mi->mesh->parentBone > -1) {
          auto boneMatrix = model->currentAnimation.globalTransforms[mi->mesh->parentBone];

          world = mi->mesh->LocalTransformMatrix() * boneMatrix * modelMat;
        } else {
          world = mi->mesh->LocalTransformMatrix() * modelMat;
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
                                     g_Surface->CurrentFrameIndex());
    }
    g_MeshStore.UpdateInstances(tmpInstances.data(), g_Scene.numMeshInstances * sizeof(MeshInstance::data), 0, g_Surface->CurrentFrameIndex());
  }

  // ImGui
  {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
  }

  g_Scene.camera->DebugWindow();

  {
    ImGui::Begin("Ray tracing");
    ImGui::Checkbox("Enable RT shadows", &g_EnableRTShadows);

    ImGui::SliderFloat("Sun Time", &g_SunTime, 0.0f, 1.0f);

    float angle = g_SunTime * XM_PI;

    float x = -XMScalarCos(angle);
    float y = -0.4f - XMScalarSin(angle) * 0.6f;
    float z = 0.0f;

    XMVECTOR vec = XMVectorSet(x, y, z, 0.0f);
    vec = XMVector3Normalize(vec);

    XMStoreFloat3(&ctx->frameConstants.SunDirection, vec);

    ImGui::Text("Sun Direction: %f %f %f", ctx->frameConstants.SunDirection.x, ctx->frameConstants.SunDirection.y,
                ctx->frameConstants.SunDirection.z);

    ImGui::End();
  }

  {
    ImGui::Begin("Timestamps");

    UINT64 timestamps[Timestamp::Count];
    ctx->timestampReadBackBuffer->Read(IssouRHI::FullBufferRange, timestamps);

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
    ImGui::Text("Raster VisBuffer: %.4f ms", GetTime(Timestamp::DrawBegin));
    ImGui::Text("Fill G-Buffer: %.4f ms", GetTime(Timestamp::FillGBufferBegin));
    ImGui::Text("Shadows RT: %.4f ms", GetTime(Timestamp::ShadowsBegin));
    ImGui::Text("Final Compose: %.4f ms", GetTime(Timestamp::FinalComposeBegin));
    ImGui::Text("Total: %.4f ms", GetTime(Timestamp::TotalBegin));

    ImGui::End();
  }

  {
    float scale = 0.25;
    auto imgSize = ImVec2((float)g_Width * scale, (float)g_Height * scale);

    ImGui::Begin("GBuffer viewer");

    if (ImGui::BeginTabBar("GBufferTabs")) {
      if (ImGui::BeginTabItem("Normal")) {
        ImGui::Image((ImTextureID)g_GBuffer.worldNormal->CreateView()->SrvDescriptorAlloc().gpuHandle.ptr, imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Position")) {
        ImGui::Image((ImTextureID)g_GBuffer.worldPosition->CreateView()->SrvDescriptorAlloc().gpuHandle.ptr, imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Base Color")) {
        ImGui::Image((ImTextureID)g_GBuffer.baseColor->CreateView()->SrvDescriptorAlloc().gpuHandle.ptr, imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Shadow")) {
        ImGui::Image((ImTextureID)g_ShadowBuffer->CreateView()->SrvDescriptorAlloc().gpuHandle.ptr, imgSize);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }
}

// TODO: move this to the RHI
struct TextureTransition {
  IssouRHI::Texture* resource;
  IssouRHI::StageAccessLayout transition;
};

struct BufferTransition {
  IssouRHI::Buffer* resource;
  IssouRHI::StageAccess transition;
};

// TODO: these should be part of the command buffer class
// when recording commands, we can deduce required StageAccessLayout
// from binding (eg resource->CreateView()->Uav() and from the command type ?
static std::array<D3D12_TEXTURE_BARRIER, 32> g_TextureBarriers{};
static std::array<D3D12_BUFFER_BARRIER, 32> g_BufferBarriers{};

template <typename TransitionT, typename BarrierT>
static size_t BuildBarriers(std::span<TransitionT> transitions, std::span<BarrierT> barriers)
{
  size_t nb = 0;

  for (auto &transition : transitions) {
    auto barrier = transition.resource->Transition(transition.transition);
    if (barrier) {
      barriers[nb++] = barrier.value();
    }
  }

  return nb;
}

void Render(float time)
{
  auto renderTarget = g_Surface->GetCurrentTexture();
  auto renderTargetView = renderTarget->CreateView();
  auto ctx = &g_FrameContext[g_Surface->CurrentFrameIndex()];

  Update(ctx, time);

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

  {
    std::array transitions{
        TextureTransition{renderTarget.get(), {D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET}},
        TextureTransition{g_VisibilityBuffer.get(), {D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET}},
    };

    auto nb = BuildBarriers<TextureTransition, D3D12_TEXTURE_BARRIER>(transitions, g_TextureBarriers);

    if (nb > 0) {
      D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(nb, g_TextureBarriers.data())};
      g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
    }
  }

  // here we again get the handle to our current render target view so we can
  // set it as the render target in the output merger stage of the pipeline
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
      g_DepthStencilBuffer->CreateView()->DsvDescriptorAlloc().cpuHandle;

  g_CommandList->ClearDepthStencilView(
      dsvHandle,
      D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  static constexpr float visBufferClearColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
  g_CommandList->ClearRenderTargetView(g_VisibilityBuffer->CreateView()->RtvDescriptorAlloc().cpuHandle, visBufferClearColor, 0, nullptr);

  // Clear the render target by using the ClearRenderTargetView command
  const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
  g_CommandList->ClearRenderTargetView(renderTargetView->RtvDescriptorAlloc().cpuHandle, clearColor, 0, nullptr);

  std::array descriptorHeaps{g_RhiDevice->SrvUavDescriptorHeap()};
  g_CommandList->SetDescriptorHeaps(static_cast<UINT>(descriptorHeaps.size()), descriptorHeaps.data());

  D3D12_VIEWPORT viewport{0.f, 0.f, (float)g_Width, (float)g_Height, 0.f, 1.f};
  g_CommandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissorRect{0, 0, static_cast<LONG>(g_Width), static_cast<LONG>(g_Height)};
  g_CommandList->RSSetScissorRects(1, &scissorRect);

  // record skinning compute commands if needed
  // TODO: we should also update culling data. And move to Indirect?
  g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::SkinBegin);
  if (g_Scene.skinnedMeshInstances.size() > 0) {
    {
      auto b = g_MeshStore.m_VertexPositions->Transition({D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS});
      if (b) {
        D3D12_BUFFER_BARRIER barriers[] = { b.value() };
        D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(_countof(barriers), barriers)};
        g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
      }
    }
    g_CommandList->SetComputeRootSignature(g_ComputeRootSignature.Get());

    g_CommandList->SetComputeRoot32BitConstants(SkinningCSRootParameter::BuffersDescriptorIndices,
                                                SizeOfInUint(SkinningBuffersDescriptorIndices),
                                                &ctx->skinningBuffersDescriptorsIndices, 0);

    for (auto smi : g_Scene.skinnedMeshInstances) {
      auto o = smi->BuffersOffsets();
      g_CommandList->SetComputeRoot32BitConstants(SkinningCSRootParameter::BuffersOffsets,
                                                  SizeOfInUint(SkinningPerDispatchConstants), &o, 0);

      g_CommandList->Dispatch(DivRoundUp(smi->numVertices, COMPUTE_GROUP_SIZE), 1, 1);
    }
  }
  g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::SkinEnd);

  // record culling commands
  {
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::CullBegin);

    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::InstanceCullingCS].Get());

    g_CommandList->SetComputeRootSignature(g_RootSignature.Get());

    g_CommandList->SetComputeRoot32BitConstants(RootParameter::FrameConstants, FrameContext::frameConstantsSize,
                                                 &ctx->frameConstants, 0);

    g_CommandList->SetComputeRoot32BitConstants(RootParameter::BuffersDescriptorIndices,
                                                SizeOfInUint(CullingBuffersDescriptorIndices),
                                                &ctx->cullingBuffersDescriptorsIndices, 0);

    g_CommandList->SetComputeRoot32BitConstant(RootParameter::PerDrawConstants, g_Scene.numMeshInstances, 0);

    {
      auto b = g_DrawMeshCommands->Transition({D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST});
      if (b) {
        D3D12_BUFFER_BARRIER barriers[] = { b.value() };
        D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(_countof(barriers), barriers)};
        g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
      }
    }

    g_CommandList->CopyBufferRegion(g_DrawMeshCommands->Resource(), DRAW_MESH_CMDS_COUNTER_OFFSET,
                                    g_UAVCounterReset->Resource(), 0, sizeof(UINT));

    {
      auto b = g_DrawMeshCommands->Transition({D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS});
      if (b) {
        D3D12_BUFFER_BARRIER barriers[] = { b.value() };
        D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(_countof(barriers), barriers)};
        g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
      }
    }

    g_CommandList->Dispatch(DivRoundUp(g_Scene.numMeshInstances, COMPUTE_GROUP_SIZE), 1, 1);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::CullEnd);
  }

  // Record drawing commands
  {
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::DrawBegin);
    {
      std::array transitions{
        BufferTransition{g_MeshStore.m_VertexPositions.get(), {D3D12_BARRIER_SYNC_DRAW, D3D12_BARRIER_ACCESS_SHADER_RESOURCE}},
        BufferTransition{g_DrawMeshCommands.get(), {D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT}},
      };

      auto nb = BuildBarriers<BufferTransition, D3D12_BUFFER_BARRIER>(transitions, g_BufferBarriers);

      if (nb > 0) {
        D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(nb, g_BufferBarriers.data())};
        g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
      }
    }

    auto rtvHandle = g_VisibilityBuffer->CreateView()->RtvDescriptorAlloc().cpuHandle;
    // TODO: RenderPass Begin/End
    g_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::BasicMS].Get());

    g_CommandList->SetGraphicsRootSignature(g_RootSignature.Get());

    g_CommandList->SetGraphicsRoot32BitConstants(RootParameter::FrameConstants, FrameContext::frameConstantsSize,
                                                 &ctx->frameConstants, 0);
    g_CommandList->SetGraphicsRoot32BitConstants(RootParameter::BuffersDescriptorIndices,
                                                 SizeOfInUint(BuffersDescriptorIndices),
                                                 &ctx->buffersDescriptorsIndices,
                                                 0);

    g_CommandList->ExecuteIndirect(g_DrawMeshCommandSignature.Get(), MESH_INSTANCE_COUNT, g_DrawMeshCommands->Resource(),
                                   0, g_DrawMeshCommands->Resource(), DRAW_MESH_CMDS_COUNTER_OFFSET);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::DrawEnd);
  }

  // Record Fill G-Buffer from Visibility-Buffer commands
  {
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::FillGBufferBegin);
    {
      std::array transitions{
          TextureTransition{g_VisibilityBuffer.get(), {D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE}},
          TextureTransition{g_GBuffer.worldPosition.get(), {D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS}},
          TextureTransition{g_GBuffer.worldNormal.get(), {D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS}},
          TextureTransition{g_GBuffer.baseColor.get(), {D3D12_BARRIER_SYNC_COMPUTE_SHADING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS}},
      };

      auto nb = BuildBarriers<TextureTransition, D3D12_TEXTURE_BARRIER>(transitions, g_TextureBarriers);

      if (nb > 0) {
        D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(nb, g_TextureBarriers.data())};
        g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
      }
    }
    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::FillGBufferCS].Get());

    auto c = g_GBuffer.PerDispatchConstants(g_VisibilityBuffer->CreateView()->SrvDescriptorAlloc().index);
    UINT n = SizeOfInUint(c);
    g_CommandList->SetComputeRoot32BitConstants(RootParameter::PerDrawConstants, n, &c, 0);

    g_CommandList->SetComputeRoot32BitConstants(RootParameter::BuffersDescriptorIndices,
                                                SizeOfInUint(BuffersDescriptorIndices), &ctx->buffersDescriptorsIndices, 0);

    g_CommandList->Dispatch(DivRoundUp(g_Width, FILL_GBUFFER_GROUP_SIZE_X), DivRoundUp(g_Height, FILL_GBUFFER_GROUP_SIZE_Y), 1);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::FillGBufferEnd);
  }

  // Ray trace shadows
  g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::ShadowsBegin);
  if (g_EnableRTShadows) {
    {
      std::array transitions{
          TextureTransition{g_ShadowBuffer.get(), {D3D12_BARRIER_SYNC_RAYTRACING, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS}},
          TextureTransition{g_GBuffer.worldPosition.get(), {D3D12_BARRIER_SYNC_RAYTRACING, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE}},
      };

      auto nb = BuildBarriers<TextureTransition, D3D12_TEXTURE_BARRIER>(transitions, g_TextureBarriers);

      if (nb > 0) {
        D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(nb, g_TextureBarriers.data())};
        g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
      }
    }

    g_CommandList->SetPipelineState1(g_DxrStateObject.Get());

    g_CommandList->SetComputeRoot32BitConstant(RootParameter::PerDrawConstants,
                                               g_GBuffer.worldPosition->CreateView()->SrvDescriptorAlloc().index, 0);
    g_CommandList->SetComputeRoot32BitConstant(RootParameter::PerDrawConstants, g_ShadowBuffer->CreateView()->UavDescriptorAlloc().index, 1);
    g_CommandList->SetComputeRoot32BitConstant(RootParameter::PerDrawConstants, g_Scene.tlasBuffer.ResultDataSrvDescriptorIndex(g_RhiDevice), 2);

    {
      D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
      // Since each shader table has only one shader record, the stride is same as the size.
      dispatchDesc.HitGroupTable.StartAddress = g_HitGroupShaderTable->GpuAddress();
      dispatchDesc.HitGroupTable.SizeInBytes = g_HitGroupShaderTable->Size();
      dispatchDesc.HitGroupTable.StrideInBytes = g_HitGroupShaderTable->Size();

      dispatchDesc.MissShaderTable.StartAddress = g_MissShaderTable->GpuAddress();
      dispatchDesc.MissShaderTable.SizeInBytes = g_MissShaderTable->Size();
      dispatchDesc.MissShaderTable.StrideInBytes = g_MissShaderTable->Size();

      dispatchDesc.RayGenerationShaderRecord.StartAddress = g_RayGenShaderTable->GpuAddress();
      dispatchDesc.RayGenerationShaderRecord.SizeInBytes = g_RayGenShaderTable->Size();

      dispatchDesc.Width = g_Width;
      dispatchDesc.Height = g_Height;
      dispatchDesc.Depth = 1;

      g_CommandList->DispatchRays(&dispatchDesc);
    }
  }
  g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::ShadowsEnd);

  // Record Full screen triangle pass - Compose final image commands
  {
    {
      std::array transitions{
          TextureTransition{g_ShadowBuffer.get(), {D3D12_BARRIER_SYNC_DRAW, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE}},
          TextureTransition{g_GBuffer.baseColor.get(), {D3D12_BARRIER_SYNC_DRAW, D3D12_BARRIER_ACCESS_SHADER_RESOURCE, D3D12_BARRIER_LAYOUT_SHADER_RESOURCE}},
      };

      auto nb = BuildBarriers<TextureTransition, D3D12_TEXTURE_BARRIER>(transitions, g_TextureBarriers);

      if (nb > 0) {
        D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(nb, g_TextureBarriers.data())};
        g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
      }
    }
    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::FinalComposeBegin);

    auto rtvHandle = renderTargetView->RtvDescriptorAlloc().cpuHandle;
    g_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::FinalComposeVS].Get());

    g_CommandList->SetGraphicsRoot32BitConstant(RootParameter::PerDrawConstants,
                                                g_GBuffer.baseColor->CreateView()->SrvDescriptorAlloc().index, 0);
    g_CommandList->SetGraphicsRoot32BitConstant(RootParameter::PerDrawConstants, g_ShadowBuffer->CreateView()->SrvDescriptorAlloc().index,
                                                1);

    g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_CommandList->DrawInstanced(3, 1, 0, 0);

    g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::FinalComposeEnd);
  }

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_CommandList.Get());

  {
    std::array transitions{
        TextureTransition{renderTarget.get(), {D3D12_BARRIER_SYNC_NONE, D3D12_BARRIER_ACCESS_NO_ACCESS, D3D12_BARRIER_LAYOUT_PRESENT}},
    };

    auto nb = BuildBarriers<TextureTransition, D3D12_TEXTURE_BARRIER>(transitions, g_TextureBarriers);

    if (nb > 0) {
      D3D12_BARRIER_GROUP barrierGroups[] = {CD3DX12_BARRIER_GROUP(nb, g_TextureBarriers.data())};
      g_CommandList->Barrier(_countof(barrierGroups), barrierGroups);
    }
  }

  g_CommandList->EndQuery(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, Timestamp::TotalEnd);

  g_CommandList->ResolveQueryData(g_TimestampQueryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, Timestamp::Count,
                                  ctx->timestampReadBackBuffer->Resource(), 0);

  CHECK_HR(g_CommandList->Close());

  // ==========

  // execute command list
  std::array ppCommandLists{static_cast<ID3D12CommandList*>(g_CommandList.Get())};
  g_CommandQueue->ExecuteCommandLists(static_cast<UINT>(ppCommandLists.size()), ppCommandLists.data());

  g_Surface->Present();
}

void Cleanup()
{
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  g_Surface->WaitForAllFrames();

  // TODO: rewrite as a class so we have RAII and can forego calling Reset() manually...

  for (auto& [k, tex] : g_Textures) {
    tex.reset();
  }

  {
    g_MeshStore.m_VertexPositions.reset();
    g_MeshStore.m_VertexNormals.reset();
    g_MeshStore.m_VertexTangents.reset();
    g_MeshStore.m_VertexUVs.reset();
    g_MeshStore.m_VertexBlendWeightsAndIndices.reset();
    g_MeshStore.m_VertexIndices.reset();
    g_MeshStore.m_Meshlets.reset();
    g_MeshStore.m_MeshletUniqueIndices.reset();
    g_MeshStore.m_MeshletPrimitives.reset();
    g_MeshStore.m_Materials.reset();

    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      g_MeshStore.m_Instances[i].reset();
      g_MeshStore.m_BoneMatrices[i].reset();
    }
  }

  g_PipelineStateObjects[PSO::BasicMS].Reset();
  g_PipelineStateObjects[PSO::SkinningCS].Reset();
  g_PipelineStateObjects[PSO::InstanceCullingCS].Reset();
  g_PipelineStateObjects[PSO::FillGBufferCS].Reset();
  g_PipelineStateObjects[PSO::FinalComposeVS].Reset();
  g_RootSignature.Reset();
  g_ComputeRootSignature.Reset();
  g_DrawMeshCommandSignature.Reset();

  g_DrawMeshCommands.reset();
  g_UAVCounterReset.reset();

  g_Scene.rtInstanceDescBuffer.reset();
  for (auto &as : g_Scene.blasBuffers) {
    as.Reset();
  }
  g_Scene.tlasBuffer.Reset();

  g_DxrStateObject.Reset();

  g_RayGenShaderTable.reset();
  g_MissShaderTable.reset();
  g_HitGroupShaderTable.reset();

  g_VisibilityBuffer.reset();
  g_GBuffer.Reset();

  g_ShadowBuffer.reset();

  g_CommandList.Reset();
  g_TimestampQueryHeap.Reset();

  g_DepthStencilBuffer.reset();

  for (size_t i = FRAME_BUFFER_COUNT; i--;) {
    g_FrameContext[i].Reset();
  }
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
  if (auto it = g_MaterialMap.find(materialPath); it != g_MaterialMap.end()) {
    return it->second->MaterialIndex();
  }

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

  material->m_MaterialBufferOffset = g_MeshStore.WriteMaterial(&material->m_GpuData, sizeof(material->m_GpuData));

  g_MaterialMap[materialPath] = material;

  return material->MaterialIndex();
}

// ========== Static functions

static void InitD3D()
{
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
}

static void InitFrameResources()
{
  // DSV
  {
    IssouRHI::TextureDesc desc{
      .label = "Depth Stencil Buffer",
      .size = {.width = g_Width, .height = g_Height},
      .mipLevelCount = 1,
      .dimension = IssouRHI::TextureDimension::Texture2D,
      .format = IssouRHI::TextureFormat::Depth32Float,
      .usage = IssouRHI::TextureUsage::RenderAttachment,
    };
    g_DepthStencilBuffer = g_RhiDevice->CreateTexture(desc);
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
  initInfo.Device = g_Device;
  initInfo.CommandQueue = g_CommandQueue;
  initInfo.NumFramesInFlight = FRAME_BUFFER_COUNT;
  initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
  // Allocating SRV descriptors (for textures) is up to the application, so we
  // provide callbacks. (current version of the backend will only allocate one
  // descriptor, future versions will need to allocate more)
  initInfo.SrvDescriptorHeap = g_RhiDevice->SrvUavDescriptorHeap();
  initInfo.SrvDescriptorAllocFn =
      [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
         D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle) {
        IssouRHI::DescriptorAllocation alloc = g_RhiDevice->AllocSrvUavDescriptor();
        *outCpuHandle = alloc.cpuHandle;
        *outGpuHandle = alloc.gpuHandle;
      };
  initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
                                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    IssouRHI::DescriptorAllocation alloc{};
    alloc.cpuHandle = cpuHandle;
    alloc.gpuHandle = gpuHandle;
    g_RhiDevice->FreeSrvUavDescriptor(alloc);
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
        RootParameter::Count, rootParameters, static_cast<UINT>(staticSamplers.size()), staticSamplers.data(), flags);

    ComPtr<ID3DBlob> signatureBlob;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                  &signatureBlob, nullptr));

    ID3D12RootSignature* rootSignature = nullptr;
    CHECK_HR(g_Device->CreateRootSignature(
        0, signatureBlob->GetBufferPointer(),
        signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
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
        .NumArgumentDescs = static_cast<UINT>(argDesc.size()),
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
    auto amplificationShaderBlob = ReadData(L"Meshlet.as.cso");
    D3D12_SHADER_BYTECODE amplificationShader = {amplificationShaderBlob.data(), amplificationShaderBlob.size()};

    auto meshShaderBlob = ReadData(L"Meshlet.ms.cso");
    D3D12_SHADER_BYTECODE meshShader = {meshShaderBlob.data(),
                                        meshShaderBlob.size()};

    auto pixelShaderBlob = ReadData(L"Meshlet.ps.cso");
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
    auto computeShaderBlob = ReadData(L"Skinning.cs.cso");
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
    auto computeShaderBlob = ReadData(L"InstanceCulling.cs.cso");
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
    auto computeShaderBlob = ReadData(L"FillGBuffer.cs.cso");
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
    auto vertexShaderBlob = ReadData(L"FullScreenTriangle.vs.cso");
    D3D12_SHADER_BYTECODE vertexShader = {vertexShaderBlob.data(), vertexShaderBlob.size()};

    auto pixelShaderBlob = ReadData(L"FinalCompose.ps.cso");
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

  // Ray traced shadow pipeline
  {
    const wchar_t* HitGroupName = L"MyHitGroup";
    const wchar_t* RaygenShaderName = L"ShadowRayGen";
    const wchar_t* AnyHitShaderName = L"ShadowAnyHit";
    const wchar_t* MissShaderName = L"ShadowMiss";

    auto libBlob = ReadData(L"RayTracing.rt.cso");
    D3D12_SHADER_BYTECODE libShader = {libBlob.data(), libBlob.size()};

    CD3DX12_STATE_OBJECT_DESC raytracingPipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
    auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    lib->SetDXILLibrary(&libShader);
    lib->DefineExport(RaygenShaderName);
    lib->DefineExport(AnyHitShaderName);
    lib->DefineExport(MissShaderName);

    auto hitGroup = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetAnyHitShaderImport(AnyHitShaderName);
    hitGroup->SetHitGroupExport(HitGroupName);
    hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payloadSize = 1 * sizeof(float);    // struct ShadowPayload { float visibility; };
    UINT attributeSize = 2 * sizeof(float);  // struct BuiltInTriangleIntersectionAttributes { float2 barycentrics; };
    shaderConfig->Config(payloadSize, attributeSize);

    auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(g_RootSignature.Get());

    auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    UINT maxRecursionDepth = 1;
    pipelineConfig->Config(maxRecursionDepth);

#ifdef _DEBUG
    PrintStateObjectDesc(raytracingPipeline);
#endif

    CHECK_HR(g_Device->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&g_DxrStateObject)));

    // Shader Table
    // TODO: ShaderTable class in RHI
    ComPtr<ID3D12StateObjectProperties> stateObjectProperties;
    CHECK_HR(g_DxrStateObject.As(&stateObjectProperties));

    UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    {
      void* shaderIdentifier = stateObjectProperties->GetShaderIdentifier(RaygenShaderName);
      UINT numShaderRecords = 1;
      IssouRHI::BufferDesc desc{
        .label = "RayGen Shader Table",
        .size = numShaderRecords * shaderIdentifierSize,
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      g_RayGenShaderTable = g_RhiDevice->CreateBuffer(desc);
      g_RayGenShaderTable->Write(IssouRHI::FullBufferRange, shaderIdentifier);
    }

    {
      void* shaderIdentifier = stateObjectProperties->GetShaderIdentifier(MissShaderName);
      UINT numShaderRecords = 1;
      IssouRHI::BufferDesc desc{
        .label = "Miss Shader Table",
        .size = numShaderRecords * shaderIdentifierSize,
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      g_MissShaderTable = g_RhiDevice->CreateBuffer(desc);
      g_MissShaderTable->Write(IssouRHI::FullBufferRange, shaderIdentifier);
    }

    {
      void* shaderIdentifier = stateObjectProperties->GetShaderIdentifier(HitGroupName);
      UINT numShaderRecords = 1;
      IssouRHI::BufferDesc desc{
        .label = "HitGroup Shader Table",
        .size = numShaderRecords * shaderIdentifierSize,
        .usage = IssouRHI::BufferUsage::MapWrite,
      };
      g_HitGroupShaderTable = g_RhiDevice->CreateBuffer(desc);
      g_HitGroupShaderTable->Write(IssouRHI::FullBufferRange, shaderIdentifier);
    }
  }

  // MeshStore
  g_MeshStore.Init(g_RhiDevice);

  // Draw Meshlets commands
  {
    IssouRHI::BufferDesc desc{
      .label = "Draw Meshlets command buffer",
      .size = DRAW_MESH_CMDS_COUNTER_OFFSET + sizeof(UINT),  // counter,
      .usage = IssouRHI::BufferUsage::CopyDst | IssouRHI::BufferUsage::Indirect | IssouRHI::BufferUsage::Storage,
    };
    g_DrawMeshCommands = g_RhiDevice->CreateBuffer(desc);
  }

  // Buffer containg just a UINT (0) used to reset UAV counter.
  {
    size_t bufSiz = sizeof(UINT);

    IssouRHI::BufferDesc desc{
      .label = "UAV Reset counter",
      .size = bufSiz,
      .usage = IssouRHI::BufferUsage::MapWrite,
    };
    g_UAVCounterReset = g_RhiDevice->CreateBuffer(desc);
    g_UAVCounterReset->Clear({0, bufSiz});
  }

  // Vis Buffer output
  {
    IssouRHI::TextureDesc desc{
      .label = "Visibility Buffer",
      .size = {.width = g_Width, .height = g_Height},
      .mipLevelCount = 1,
      .dimension = IssouRHI::TextureDimension::Texture2D,
      .format = IssouRHI::TextureFormat::R32Uint,
      .usage = IssouRHI::TextureUsage::RenderAttachment | IssouRHI::TextureUsage::TextureBinding,
    };
    g_VisibilityBuffer = g_RhiDevice->CreateTexture(desc);
  }

  // G-Buffer output
  {
    IssouRHI::TextureDesc desc{
      .label = "G-Buffer world position",
      .size = {.width = g_Width, .height = g_Height},
      .mipLevelCount = 1,
      .dimension = IssouRHI::TextureDimension::Texture2D,
      .format = IssouRHI::TextureFormat::RGBA32Float,
      .usage = IssouRHI::TextureUsage::TextureBinding | IssouRHI::TextureUsage::StorageBinding,
    };
    g_GBuffer.worldPosition = g_RhiDevice->CreateTexture(desc);
  }
  {
    IssouRHI::TextureDesc desc{
      .label = "G-Buffer world normal",
      .size = {.width = g_Width, .height = g_Height},
      .mipLevelCount = 1,
      .dimension = IssouRHI::TextureDimension::Texture2D,
      .format = IssouRHI::TextureFormat::RGB10A2Unorm,
      .usage = IssouRHI::TextureUsage::TextureBinding | IssouRHI::TextureUsage::StorageBinding,
    };
    g_GBuffer.worldNormal = g_RhiDevice->CreateTexture(desc);
  }
  {
    IssouRHI::TextureDesc desc{
      .label = "G-Buffer base color",
      .size = {.width = g_Width, .height = g_Height},
      .mipLevelCount = 1,
      .dimension = IssouRHI::TextureDimension::Texture2D,
      .format = IssouRHI::TextureFormat::RGBA8Unorm,
      .usage = IssouRHI::TextureUsage::TextureBinding | IssouRHI::TextureUsage::StorageBinding,
    };
    g_GBuffer.baseColor = g_RhiDevice->CreateTexture(desc);
  }
  // Shadow buffer output
  {
    IssouRHI::TextureDesc desc{
      .label = "Shadow buffer",
      .size = {.width = g_Width, .height = g_Height},
      .mipLevelCount = 1,
      .dimension = IssouRHI::TextureDimension::Texture2D,
      .format = IssouRHI::TextureFormat::R8Unorm,
      .usage = IssouRHI::TextureUsage::TextureBinding | IssouRHI::TextureUsage::StorageBinding,
    };
    g_ShadowBuffer = g_RhiDevice->CreateTexture(desc);
  }

  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    auto ctx = &g_FrameContext[i];

    ctx->buffersDescriptorsIndices = g_MeshStore.BuffersDescriptorIndices(static_cast<UINT>(i));
    ctx->skinningBuffersDescriptorsIndices = g_MeshStore.SkinningBuffersDescriptorIndices(static_cast<UINT>(i));
    ctx->cullingBuffersDescriptorsIndices = {
        .InstancesBufferId = g_MeshStore.InstancesBufferId(static_cast<UINT>(i)),
        .DrawMeshCommandsBufferId = g_DrawMeshCommands->UavDescriptorAlloc(IssouRHI::FullBufferRange, sizeof(DrawMeshCommand), g_DrawMeshCommands.get(), DRAW_MESH_CMDS_COUNTER_OFFSET).index,
    };
  }

  // timestamp readback buffer
  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    IssouRHI::BufferDesc desc{
      .label = std::format("Timestamp Readback Buffer {}", i),
      .size = sizeof(UINT64) * Timestamp::Count,
      .usage = IssouRHI::BufferUsage::MapRead,
    };
    g_FrameContext[i].timestampReadBackBuffer = g_RhiDevice->CreateBuffer(desc);
  }
}

// TODO: rewrite this mess
static std::shared_ptr<MeshInstance> LoadMesh3D(std::shared_ptr<Mesh3D> mesh)
{
  auto meshBasePath = std::filesystem::path(mesh->name).parent_path();

  // Create MeshInstance
  auto mi = std::make_shared<MeshInstance>();
  mi->instanceBufferOffset = g_MeshStore.ReserveInstance(sizeof(MeshInstance::data));
  mi->mesh = mesh;

  // Assign it to the meshlets of this instance
  std::vector<MeshletData> instanceMeshlets = mesh->meshlets;
  mi->data.numMeshlets = static_cast<UINT>(mesh->meshlets.size());
  for (auto& m : instanceMeshlets) {
    m.instanceIndex = mi->instanceBufferOffset / sizeof(MeshInstance::data);
  }

  {
    auto it = g_Scene.meshInstanceMap.find(mesh->name);
    if (it == std::end(g_Scene.meshInstanceMap)) { // first time seeing this mesh
      // CreateGeometry
      // vertex data
      if (mesh->Skinned()) {
        // in case of skinned mesh, these will be filled by a compute shader
        mi->data.firstPosition = g_MeshStore.ReservePositions(mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstNormal = g_MeshStore.ReserveNormals(mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstTangent = g_MeshStore.ReserveTangents(mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);

        auto smi = std::make_shared<SkinnedMeshInstance>();
        smi->offsets.basePositionsBuffer =
            g_MeshStore.WritePositions(mesh->positions.data(), mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        smi->offsets.baseNormalsBuffer =
            g_MeshStore.WriteNormals(mesh->normals.data(), mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        smi->offsets.baseTangentsBuffer =
            g_MeshStore.WriteTangents(mesh->tangents.data(), mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);
        smi->offsets.blendWeightsAndIndicesBuffer =
            g_MeshStore.WriteBWI(mesh->blendWeightsAndIndices.data(), mesh->BlendWeightsAndIndicesBufferSize()) /
            sizeof(XMUINT2);

        smi->numVertices = mesh->header.numVerts;
        smi->numBoneMatrices = static_cast<UINT>(mesh->SkinMatricesSize());
        // TODO: should reuse bone matrice buffer for meshes of same model which share skin
        smi->offsets.boneMatricesBuffer =
            g_MeshStore.ReserveBoneMatrices(mesh->SkinMatricesBufferSize()) / sizeof(XMFLOAT4X4);

        smi->meshInstance = mi;
        mi->skinnedMeshInstance = smi;

        g_Scene.skinnedMeshInstances.push_back(smi);
        g_Scene.numBoneMatrices += smi->numBoneMatrices;
      } else { // if not skinned
        mi->data.firstPosition =
            g_MeshStore.WritePositions(mesh->positions.data(), mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstNormal =
            g_MeshStore.WriteNormals(mesh->normals.data(), mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstTangent =
            g_MeshStore.WriteTangents(mesh->tangents.data(), mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);
      }

      mi->data.firstUV = g_MeshStore.WriteUVs(mesh->uvs.data(), mesh->UvsBufferSize()) / sizeof(XMFLOAT2);
      mi->indexBufferOffset = g_MeshStore.WriteIndices(mesh->indices.data(), mesh->IndicesBufferSize());

      // meshlet data
      mi->data.firstMeshlet =
          g_MeshStore.WriteMeshlets(instanceMeshlets.data(), mesh->MeshletBufferSize()) / sizeof(MeshletData);
      mi->data.firstVertIndex =
          g_MeshStore.WriteMeshletUniqueIndices(mesh->uniqueVertexIndices.data(), mesh->MeshletIndexBufferSize()) /
          sizeof(UINT);
      mi->data.firstPrimitive =
          g_MeshStore.WriteMeshletPrimitives(mesh->primitiveIndices.data(), mesh->MeshletPrimitiveBufferSize()) /
          sizeof(UINT);

      if (!mesh->Skinned()) g_Scene.uniqueMeshInstances.push_back(mi); // TODO: only non skinned mesh for now
    } else { // an instance for this mesh already exists
      auto i = it->second[0];
      mi->data.firstPosition = i->data.firstPosition;
      mi->data.firstNormal = i->data.firstNormal;
      mi->data.firstTangent = i->data.firstTangent;
      mi->data.firstUV = i->data.firstUV;

      mi->data.firstMeshlet =
          g_MeshStore.WriteMeshlets(instanceMeshlets.data(), mesh->MeshletBufferSize()) / sizeof(MeshletData);
      mi->data.firstVertIndex = i->data.firstVertIndex;
      mi->data.firstPrimitive = i->data.firstPrimitive;

      mi->indexBufferOffset = i->indexBufferOffset;

      if (mesh->Skinned()) {
        // these will be filled by compute shader so we need new ones.
        mi->data.firstPosition = g_MeshStore.ReservePositions(mesh->PositionsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstNormal = g_MeshStore.ReserveNormals(mesh->NormalsBufferSize()) / sizeof(XMFLOAT3);
        mi->data.firstTangent = g_MeshStore.ReserveTangents(mesh->TangentsBufferSize()) / sizeof(XMFLOAT4);

        auto smi = std::make_shared<SkinnedMeshInstance>();
        smi->numVertices = mesh->header.numVerts;
        if (auto iSmi = i->skinnedMeshInstance.lock()) {
          smi->offsets = iSmi->offsets;
          smi->numBoneMatrices = iSmi->numBoneMatrices;
        }
        // TODO: should reuse bone matrice buffer for meshes of same model which share skin
        smi->offsets.boneMatricesBuffer =
            g_MeshStore.ReserveBoneMatrices(smi->BoneMatricesBufferSize()) / sizeof(XMFLOAT4X4);

        smi->meshInstance = mi;
        mi->skinnedMeshInstance = smi;

        g_Scene.skinnedMeshInstances.push_back(smi);
        g_Scene.numBoneMatrices += smi->numBoneMatrices;

        // a skinned mesh instance counts as unique mesh instance even if mesh already seen
        // g_Scene.uniqueMeshInstances.push_back(mi); // skip for now
      }
    }
  }

  g_Scene.meshInstanceMap[mesh->name].push_back(mi);
  g_Scene.numMeshInstances++;

  return mi;
}

static UINT CreateTexture(std::filesystem::path filename)
{
  if (auto it = g_Textures.find(filename); it != g_Textures.end()) {
    return it->second->CreateView()->SrvDescriptorAlloc().index;
  }

  TexMetadata metadata;
  ScratchImage image;

  LoadFromDDSFile(filename.wstring().c_str(), DDS_FLAGS_NONE, &metadata, image);

  auto texDimension = [dim = metadata.dimension]() {
    switch (dim) {
      case TEX_DIMENSION_TEXTURE1D:
        return IssouRHI::TextureDimension::Texture1D;
      case TEX_DIMENSION_TEXTURE2D:
        return IssouRHI::TextureDimension::Texture2D;
      case TEX_DIMENSION_TEXTURE3D:
        return IssouRHI::TextureDimension::Texture3D;
    }
  };
  auto texFormat = [format = metadata.format]() {
    switch (format) {
      case DXGI_FORMAT_BC5_UNORM:
        return IssouRHI::TextureFormat::BC5Unorm;
      case DXGI_FORMAT_BC7_UNORM:
        return IssouRHI::TextureFormat::BC7Unorm;
      default:
        // don't support anything else for now
        std::unreachable();
    }
  };
  IssouRHI::TextureDesc textureDesc{
      .label = filename.string().c_str(),
      .size = {
        .width = static_cast<uint32_t>(metadata.width),
        .height = static_cast<uint32_t>(metadata.height),
        .depth = static_cast<uint32_t>(metadata.arraySize),
      },
      .mipLevelCount = static_cast<uint32_t>(metadata.mipLevels),
      .dimension = texDimension(),
      .format = texFormat(),
      .usage = IssouRHI::TextureUsage::CopyDst | IssouRHI::TextureUsage::TextureBinding,
  };
  auto tex = g_RhiDevice->CreateTexture(textureDesc);

  std::vector<D3D12_SUBRESOURCE_DATA> subresources;
  CHECK_HR(PrepareUpload(g_Device, image.GetImages(), image.GetImageCount(), metadata, subresources));

  tex->WriteToSubresource(subresources.data(), static_cast<UINT>(subresources.size()));

  g_Textures[filename] = tex;

  return tex->CreateView()->SrvDescriptorAlloc().index;
}
}  // namespace Renderer
