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

static constexpr size_t MESH_INSTANCE_COUNT = 10'000;

// ========== Enums

enum class PSO { SkinningCS, InstanceCullingCS, FillGBufferCS };

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

static constexpr UINT DRAW_MESH_CMDS_SIZE = MESH_INSTANCE_COUNT * sizeof(DrawMeshCommand);
static constexpr UINT DRAW_MESH_CMDS_COUNTER_OFFSET = AlignForUavCounter(DRAW_MESH_CMDS_SIZE);

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
    std::vector<std::shared_ptr<SkinnedMeshInstance>> skinnedMeshInstances;  // should be per Skin, not per Model...
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

  std::vector<std::shared_ptr<IssouRHI::AccelerationStructure>> blasBuffers;
  std::shared_ptr<IssouRHI::AccelerationStructure> tlasBuffer;
  std::vector<IssouRHI::TopLevelInstanceDesc> rtInstanceDescriptors;

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

  std::shared_ptr<IssouRHI::Buffer> frameConstantBuffer;
  std::shared_ptr<IssouRHI::Buffer> timestampReadBackBuffer;

  void UpdateFrameConstants()
  {
    frameConstantBuffer->Write(IssouRHI::FullBufferRange, &frameConstants);
  }

  void Reset()
  {
    frameConstantBuffer.reset();
    timestampReadBackBuffer.reset();
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
        .vertexPositionsBufferId = m_VertexPositions->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(XMFLOAT3)}),
        .vertexNormalsBufferId = m_VertexNormals->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(XMFLOAT3)}),
        .vertexTangentsBufferId = m_VertexTangents->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(XMFLOAT4)}),
        .vertexUVsBufferId = m_VertexUVs->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(XMFLOAT2)}),

        .meshletsBufferId = m_Meshlets->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(MeshletData)}),
        .meshletVertIndicesBufferId = m_MeshletUniqueIndices->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(UINT)}),
        .meshletsPrimitivesBufferId = m_MeshletPrimitives->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(MeshletTriangle)}),

        .materialsBufferId = m_Materials->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(Material::m_GpuData)}),
        .instancesBufferId = m_Instances[frameIndex]->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(MeshInstance::data)}),
    };
  }

  SkinningBuffersDescriptorIndices SkinningBuffersDescriptorIndices(UINT frameIndex) const
  {
    return {
        .vertexPositionsBufferId = m_VertexPositions->DescriptorIndex({IssouRHI::BufferAccess::ReadWrite, IssouRHI::FullBufferRange, sizeof(XMFLOAT3)}),
        .vertexNormalsBufferId = m_VertexNormals->DescriptorIndex({IssouRHI::BufferAccess::ReadWrite, IssouRHI::FullBufferRange, sizeof(XMFLOAT3)}),
        .vertexTangentsBufferId = m_VertexTangents->DescriptorIndex({IssouRHI::BufferAccess::ReadWrite, IssouRHI::FullBufferRange, sizeof(XMFLOAT4)}),
        .vertexBlendWeightsAndIndicesBufferId = m_VertexBlendWeightsAndIndices->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(XMUINT2)}),
        .boneMatricesBufferId = m_BoneMatrices[frameIndex]->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(XMFLOAT4X4)}),
    };
  }

  UINT InstancesBufferId(UINT frameIndex) const
  {
    return m_Instances[frameIndex]->DescriptorIndex({IssouRHI::BufferAccess::Read, IssouRHI::FullBufferRange, sizeof(MeshInstance::data)});
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

  std::shared_ptr<IssouRHI::Buffer> m_VertexIndices;  // needed for BLAS

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

static void InitFrameResources();
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
static IssouRHI::Device* g_Device;
static IssouRHI::Surface* g_Surface;

static FrameContext g_FrameContext[FRAME_BUFFER_COUNT];

// Resources
static std::shared_ptr<IssouRHI::Texture> g_DepthStencilBuffer;
static std::shared_ptr<IssouRHI::QuerySet> g_TimestampQuerySet;

// PSO
static std::shared_ptr<IssouRHI::RenderPipeline> g_RenderPipeline;
static std::shared_ptr<IssouRHI::MeshPipeline> g_MeshPipeline;
static std::shared_ptr<IssouRHI::RayTracingPipeline> g_RayTracingPipeline;
static std::unordered_map<PSO, std::shared_ptr<IssouRHI::ComputePipeline>> g_ComputePipelines;

static std::shared_ptr<IssouRHI::ShaderTable> g_ShaderTable;

static std::shared_ptr<IssouRHI::Buffer> g_DrawMeshCommands;  // written by compute shader
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
        .WorldPositionId = worldPosition->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::ReadWrite),
        .WorldNormalId = worldNormal->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::ReadWrite),
        .BaseColorId = baseColor->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::ReadWrite),
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
  g_Device = device.get();    // FIXME: TMP raw ptr!
  g_Surface = surface.get();  // FIXME: TMP raw ptr!

  InitFrameResources();
}

void LoadAssets()
{
  for (auto& node : g_Scene.nodes) {
    for (auto& mesh : node.model->meshes) {
      auto mi = LoadMesh3D(mesh);

      node.meshInstances.push_back(mi);
      if (auto smi = mi->skinnedMeshInstance.lock()) {
        node.skinnedMeshInstances.push_back(smi);
      }
    }
  }

  // RayTracing acceleration structures setup
  auto queue = g_Device->GetQueue();
  auto encoder = queue->CreateCommandEncoder();

  // BLAS creation
  {
    const size_t numMeshes = g_Scene.uniqueMeshInstances.size();

    std::vector<IssouRHI::AccelerationStructureDesc> bottomLevelInputs(numMeshes);

    g_Scene.blasBuffers.resize(numMeshes);

    for (size_t i = 0; i < numMeshes; i++) {
      auto& mi = g_Scene.uniqueMeshInstances[i];
      auto& mesh = mi->mesh;

      // TODO: loop subsets
      std::array geometries{
          IssouRHI::BottomLevelGeometryDesc{
              .flags = IssouRHI::BottomLevelGeometryFlags::Opaque,
              .geometry = IssouRHI::BottomLevelTrianglesDesc{
                  .vertices = {g_MeshStore.m_VertexPositions.get(), mi->data.firstPosition * sizeof(XMFLOAT3)},
                  .vertexStride = sizeof(XMFLOAT3),
                  .vertexCount = mesh->header.numVerts,
                  .vertexFormat = IssouRHI::VertexFormat::Float32x3,
                  .indices = {g_MeshStore.m_VertexIndices.get(), mi->indexBufferOffset},
                  .indexCount = mesh->header.numIndices,
                  .indexFormat = IssouRHI::IndexFormat::Uint32,
              },
          },
      };

      bottomLevelInputs[i] = {
          .label = std::format("BLAS {}", i),
          .flags = IssouRHI::AccelerationStructureFlags::PreferFastTrace,
          .geometryOrInstanceDesc = IssouRHI::BottomLevelDesc{
              .geometries = geometries,
          },
      };

      g_Scene.blasBuffers[i] = g_Device->CreateAccelerationStructure(bottomLevelInputs[i]);

      // assign blas buffer address to each instances of this mesh
      for (auto& inst : g_Scene.meshInstanceMap[mesh->name]) {
        inst->blasBufferAddress = g_Scene.blasBuffers[i]->GpuAddress();
      }

      encoder.BuildBottomLevelAccelerationStructure(g_Scene.blasBuffers[i].get(), geometries);
    }
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

        IssouRHI::TopLevelInstanceDesc desc{};
        XMStoreFloat3x4(reinterpret_cast<XMFLOAT3X4*>(desc.transformMatrix), world);
        // desc.instanceId;
        desc.instanceMask = 0xff;
        // desc.instanceContributionToHitGroupIndex;
        // desc.flags;
        desc.accelerationStructureGpuAddress = mi->blasBufferAddress;
        assert(desc.accelerationStructureGpuAddress != 0);

        g_Scene.rtInstanceDescriptors.push_back(desc);
      }
    }
  }

  // RT instance descriptors buffer
  std::shared_ptr<IssouRHI::Buffer> rtInstanceDescBuffer;
  {
    IssouRHI::BufferDesc desc{
        .label = "RT Instance Desc Buffer",
        .size = sizeof(IssouRHI::TopLevelInstanceDesc) * g_Scene.rtInstanceDescriptors.size(),
        .usage = IssouRHI::BufferUsage::MapWrite,
    };
    rtInstanceDescBuffer = g_Device->CreateBuffer(desc);
    rtInstanceDescBuffer->Write(IssouRHI::FullBufferRange, g_Scene.rtInstanceDescriptors.data());
  }

  {
    std::array transitions{
        IssouRHI::GlobalBarrierDesc{
            .from = IssouRHI::StageAccess{.stage = IssouRHI::PipelineStage::AccelerationStructure, .access = IssouRHI::Access::AccelerationStructureWrite},
            .to = IssouRHI::StageAccess{.stage = IssouRHI::PipelineStage::AccelerationStructure, .access = IssouRHI::Access::AccelerationStructureRead},
        },
    };

    encoder.Barrier({.globals = transitions});
  }

  // TLAS creation
  {
    IssouRHI::AccelerationStructureDesc topLevelInputs{
        .label = "TLAS",
        .flags = IssouRHI::AccelerationStructureFlags::PreferFastTrace,
        .geometryOrInstanceDesc = IssouRHI::TopLevelDesc{
            .instances = g_Scene.rtInstanceDescriptors,
        },
    };

    g_Scene.tlasBuffer = g_Device->CreateAccelerationStructure(topLevelInputs);

    encoder.BuildTopLevelAccelerationStructure(g_Scene.tlasBuffer.get(), {rtInstanceDescBuffer.get(), 0}, g_Scene.rtInstanceDescriptors.size());
  }

  IssouRHI::CommandBuffer* cb[] = {encoder.Finish()};
  queue->Submit(cb);
  queue->WaitForAll();
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
    XMStoreFloat4x4(&ctx->frameConstants.ViewProj, vp);
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
        for (auto& [k, skin] : model->skins) {
          std::vector<XMFLOAT4X4> matrices = model->currentAnimation.BoneTransforms(time, skin.get());

          for (auto& smi : node.skinnedMeshInstances) {
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

        XMMATRIX normalMatrix = XMMatrixInverse(nullptr, world);

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

  ctx->UpdateFrameConstants();

  {
    ImGui::Begin("Timestamps");

    UINT64 timestamps[Timestamp::Count];
    ctx->timestampReadBackBuffer->Read(IssouRHI::FullBufferRange, timestamps);

    UINT64 frequency = g_Device->TimestampFrequencyHz();

    auto GetTime = [&frequency, &timestamps](size_t i) {
      UINT64 begin = timestamps[i];
      UINT64 end = timestamps[i + 1];
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
        ImGui::Image((ImTextureID)g_GBuffer.worldNormal->CreateView()->DescriptorHandle(IssouRHI::TextureAccess::Read), imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Position")) {
        ImGui::Image((ImTextureID)g_GBuffer.worldPosition->CreateView()->DescriptorHandle(IssouRHI::TextureAccess::Read), imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Base Color")) {
        ImGui::Image((ImTextureID)g_GBuffer.baseColor->CreateView()->DescriptorHandle(IssouRHI::TextureAccess::Read), imgSize);
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Shadow")) {
        ImGui::Image((ImTextureID)g_ShadowBuffer->CreateView()->DescriptorHandle(IssouRHI::TextureAccess::Read), imgSize);
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }
}

static std::unordered_map<IssouRHI::Texture*, IssouRHI::StageAccessLayout> g_TextureStates;
static std::unordered_map<IssouRHI::Buffer*, IssouRHI::StageAccess> g_BufferStates;

static IssouRHI::StageAccessLayout GetTextureState(IssouRHI::Texture* tex)
{
  auto it = g_TextureStates.find(tex);
  if (it != std::end(g_TextureStates)) {
    return it->second;
  }

  IssouRHI::StageAccessLayout state{
      .stage = IssouRHI::PipelineStage::None,
      .access = IssouRHI::Access::None,
      .layout = IssouRHI::TextureLayout::General,
  };
  g_TextureStates[tex] = state;

  return state;
}

static IssouRHI::StageAccess GetBufferState(IssouRHI::Buffer* buf)
{
  auto it = g_BufferStates.find(buf);
  if (it != std::end(g_BufferStates)) {
    return it->second;
  }

  IssouRHI::StageAccess state{
      .stage = IssouRHI::PipelineStage::None,
      .access = IssouRHI::Access::None,
  };
  g_BufferStates[buf] = state;

  return state;
}

// TODO: should we do barrier elision here, or leave it to the RHI?
static IssouRHI::BufferBarrierDesc BuildTransition(IssouRHI::Buffer* buf, IssouRHI::StageAccess to)
{
  auto from = GetBufferState(buf);

  IssouRHI::BufferBarrierDesc desc{
      .resource = buf,
      .from = from,
      .to = to,
  };

  g_BufferStates[buf] = to;

  return desc;
}

// TODO: should we do barrier elision here, or leave it to the RHI?
static IssouRHI::TextureBarrierDesc BuildTransition(IssouRHI::Texture* tex, IssouRHI::StageAccessLayout to)
{
  auto from = GetTextureState(tex);

  IssouRHI::TextureBarrierDesc desc{
      .resource = tex,
      .from = from,
      .to = to,
  };

  g_TextureStates[tex] = to;

  return desc;
}

void Render(float time)
{
  auto renderTarget = g_Surface->GetCurrentTexture();
  auto renderTargetView = renderTarget->CreateView();
  auto ctx = &g_FrameContext[g_Surface->CurrentFrameIndex()];

  Update(ctx, time);

  auto queue = g_Device->GetQueue();
  auto encoder = queue->CreateCommandEncoder();

  encoder.WriteTimestamp(g_TimestampQuerySet.get(), Timestamp::TotalBegin);

  {
    std::array transitions{
        BuildTransition(renderTarget.get(), {IssouRHI::PipelineStage::ColorAttachment, IssouRHI::Access::ColorAttachmentWrite, IssouRHI::TextureLayout::ColorAttachment}),
        BuildTransition(g_VisibilityBuffer.get(), {IssouRHI::PipelineStage::ColorAttachment, IssouRHI::Access::ColorAttachmentWrite, IssouRHI::TextureLayout::ColorAttachment}),
    };

    encoder.Barrier({.textures = transitions});
  }

  uint32_t frameConstantsIndex = ctx->frameConstantBuffer->DescriptorIndex({IssouRHI::BufferAccess::Constant, IssouRHI::FullBufferRange, sizeof(FrameConstants)});

  // record skinning compute commands if needed
  // TODO: we should also update culling data. And move to Indirect?
  if (g_Scene.skinnedMeshInstances.size() > 0) {
    {
      std::array transitions{
          BuildTransition(g_MeshStore.m_VertexPositions.get(), {IssouRHI::PipelineStage::ComputeShader, IssouRHI::Access::ShaderResourceStorage}),
      };

      encoder.Barrier({.buffers = transitions});
    }

    auto passEncoder = encoder.BeginComputePass({
        .label = "Skinning Compute Pass",
        .timestampWrites = IssouRHI::TimestampWrites{
            .beginningOfPassWriteIndex = Timestamp::SkinBegin,
            .endOfPassWriteIndex = Timestamp::SkinEnd,
            .querySet = g_TimestampQuerySet.get(),
        },
    });

    passEncoder.SetPipeline(g_ComputePipelines[PSO::SkinningCS].get());
    passEncoder.PushConstants(0, SizeOfInUint(SkinningBuffersDescriptorIndices), &ctx->skinningBuffersDescriptorsIndices);

    for (auto smi : g_Scene.skinnedMeshInstances) {
      auto o = smi->BuffersOffsets();
      passEncoder.PushConstants(SizeOfInUint(SkinningBuffersDescriptorIndices), SizeOfInUint(o), &o);
      passEncoder.Dispatch(DivRoundUp(smi->numVertices, COMPUTE_GROUP_SIZE));
    }

    passEncoder.End();
  } else {
    encoder.WriteTimestamp(g_TimestampQuerySet.get(), Timestamp::SkinBegin);
    encoder.WriteTimestamp(g_TimestampQuerySet.get(), Timestamp::SkinEnd);
  }

  // record culling commands
  {
    {
      std::array transitions{
          BuildTransition(g_DrawMeshCommands.get(), {IssouRHI::PipelineStage::Copy, IssouRHI::Access::CopyDestination}),
      };

      encoder.Barrier({.buffers = transitions});
    }

    encoder.CopyBufferToBuffer(g_UAVCounterReset.get(), 0, g_DrawMeshCommands.get(), DRAW_MESH_CMDS_COUNTER_OFFSET, sizeof(UINT));

    {
      std::array transitions{
          BuildTransition(g_DrawMeshCommands.get(), {IssouRHI::PipelineStage::ComputeShader, IssouRHI::Access::ShaderResourceStorage}),
      };

      encoder.Barrier({.buffers = transitions});
    }

    auto passEncoder = encoder.BeginComputePass({
        .label = "Culling Compute Pass",
        .timestampWrites = IssouRHI::TimestampWrites{
            .beginningOfPassWriteIndex = Timestamp::CullBegin,
            .endOfPassWriteIndex = Timestamp::CullEnd,
            .querySet = g_TimestampQuerySet.get(),
        },
    });

    passEncoder.SetPipeline(g_ComputePipelines[PSO::InstanceCullingCS].get());

    passEncoder.PushConstants(0, SizeOfInUint(CullingBuffersDescriptorIndices), &ctx->cullingBuffersDescriptorsIndices);
    passEncoder.PushConstants(SizeOfInUint(CullingBuffersDescriptorIndices), 1, &frameConstantsIndex);
    passEncoder.PushConstants(SizeOfInUint(CullingBuffersDescriptorIndices) + 1, 1, &g_Scene.numMeshInstances);

    passEncoder.Dispatch(DivRoundUp(g_Scene.numMeshInstances, COMPUTE_GROUP_SIZE));

    passEncoder.End();
  }

  // Record drawing commands
  {
    {
      std::array transitions{
          BuildTransition(g_MeshStore.m_VertexPositions.get(), {IssouRHI::PipelineStage::MeshShaders, IssouRHI::Access::ShaderResource}),
          BuildTransition(g_DrawMeshCommands.get(), {IssouRHI::PipelineStage::Indirect, IssouRHI::Access::ArgumentBuffer}),
      };

      encoder.Barrier({.buffers = transitions});
    }

    std::array targets{
        IssouRHI::ColorAttachment{
            .view = g_VisibilityBuffer->CreateView().get(),
            .clearValue = {0.0f, 0.0f, 0.0f, 0.0f},
        },
    };
    auto passEncoder = encoder.BeginMeshPass({
        .label = "Visibilty Buffer Pass",
        .colorAttachment = targets,
        .depthStencilAttachment = {
            .view = g_DepthStencilBuffer->CreateView().get(),
            .depthClearValue = 1.0f,
        },
        .timestampWrites = IssouRHI::TimestampWrites{
            .beginningOfPassWriteIndex = Timestamp::DrawBegin,
            .endOfPassWriteIndex = Timestamp::DrawEnd,
            .querySet = g_TimestampQuerySet.get(),
        },
    });

    passEncoder.SetPipeline(g_MeshPipeline.get());

    passEncoder.PushConstants(0, SizeOfInUint(BuffersDescriptorIndices), &ctx->buffersDescriptorsIndices);
    passEncoder.PushConstants(SizeOfInUint(BuffersDescriptorIndices), 1, &frameConstantsIndex);

    passEncoder.DrawMeshIndirect(g_DrawMeshCommands.get(), 0, MESH_INSTANCE_COUNT, g_DrawMeshCommands.get(), DRAW_MESH_CMDS_COUNTER_OFFSET);

    passEncoder.End();
  }

  // Record Fill G-Buffer from Visibility-Buffer commands
  {
    {
      std::array transitions{
          BuildTransition(g_VisibilityBuffer.get(), {IssouRHI::PipelineStage::ComputeShader, IssouRHI::Access::ShaderResource, IssouRHI::TextureLayout::ShaderResource}),
          BuildTransition(g_GBuffer.worldPosition.get(), {IssouRHI::PipelineStage::ComputeShader, IssouRHI::Access::ShaderResourceStorage, IssouRHI::TextureLayout::ShaderResourceStorage}),
          BuildTransition(g_GBuffer.worldNormal.get(), {IssouRHI::PipelineStage::ComputeShader, IssouRHI::Access::ShaderResourceStorage, IssouRHI::TextureLayout::ShaderResourceStorage}),
          BuildTransition(g_GBuffer.baseColor.get(), {IssouRHI::PipelineStage::ComputeShader, IssouRHI::Access::ShaderResourceStorage, IssouRHI::TextureLayout::ShaderResourceStorage}),
      };

      encoder.Barrier({.textures = transitions});
    }

    auto passEncoder = encoder.BeginComputePass({
        .label = "Fill G-Buffer Compute Pass",
        .timestampWrites = IssouRHI::TimestampWrites{
            .beginningOfPassWriteIndex = Timestamp::FillGBufferBegin,
            .endOfPassWriteIndex = Timestamp::FillGBufferEnd,
            .querySet = g_TimestampQuerySet.get(),
        },
    });

    passEncoder.SetPipeline(g_ComputePipelines[PSO::FillGBufferCS].get());

    auto c = g_GBuffer.PerDispatchConstants(g_VisibilityBuffer->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::Read));
    UINT n = SizeOfInUint(c);
    UINT n2 = SizeOfInUint(ctx->buffersDescriptorsIndices);

    // TODO: instead of beeing dumb. only do SetComputeRoot32BitConstants once at the beginning of frame
    // same for SetGraphicsRoot32BitConstant. "bind" everything up front, and be done with it.
    // Only have one "slot", so only one "InitAsConstant" and prepare some enum for the different offsets in the root signature.
    // Also, instead of writing an entire struct to the root signature. have ConstantBuffer and write the cbv to it.
    passEncoder.PushConstants(0, n, &c);
    passEncoder.PushConstants(n, n2, &ctx->buffersDescriptorsIndices);
    passEncoder.PushConstants(n + n2, 1, &frameConstantsIndex);

    passEncoder.Dispatch(DivRoundUp(g_Width, FILL_GBUFFER_GROUP_SIZE_X), DivRoundUp(g_Height, FILL_GBUFFER_GROUP_SIZE_Y));

    passEncoder.End();
  }

  // Ray trace shadows
  encoder.WriteTimestamp(g_TimestampQuerySet.get(), Timestamp::ShadowsBegin);
  if (g_EnableRTShadows) {
    {
      std::array transitions{
          BuildTransition(g_ShadowBuffer.get(), {IssouRHI::PipelineStage::RayTracingShaders, IssouRHI::Access::ShaderResourceStorage, IssouRHI::TextureLayout::ShaderResourceStorage}),
          BuildTransition(g_GBuffer.worldPosition.get(), {IssouRHI::PipelineStage::RayTracingShaders, IssouRHI::Access::ShaderResource, IssouRHI::TextureLayout::ShaderResource}),
      };

      encoder.Barrier({.textures = transitions});
    }

    encoder.CommandList()->SetPipelineState1(g_RayTracingPipeline->StateObject());

    std::array<uint32_t, 4> pc = {
        g_GBuffer.worldPosition->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::Read),
        g_ShadowBuffer->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::ReadWrite),
        g_Scene.tlasBuffer->DescriptorIndex(),
        frameConstantsIndex,
    };

    encoder.CommandList()->SetComputeRoot32BitConstants(0, pc.size(), pc.data(), 0);

    {
      D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
      dispatchDesc.HitGroupTable = g_ShaderTable->HitGroupTable();
      dispatchDesc.MissShaderTable = g_ShaderTable->MissShaderTable();
      dispatchDesc.RayGenerationShaderRecord = g_ShaderTable->RayGenShaderRecord();

      dispatchDesc.Width = g_Width;
      dispatchDesc.Height = g_Height;
      dispatchDesc.Depth = 1;

      encoder.CommandList()->DispatchRays(&dispatchDesc);
    }
  }
  encoder.WriteTimestamp(g_TimestampQuerySet.get(), Timestamp::ShadowsEnd);

  // Record Full screen triangle pass - Compose final image commands
  {
    {
      std::array transitions{
          BuildTransition(g_ShadowBuffer.get(), {IssouRHI::PipelineStage::FragmentShader, IssouRHI::Access::ShaderResource, IssouRHI::TextureLayout::ShaderResource}),
          BuildTransition(g_GBuffer.baseColor.get(), {IssouRHI::PipelineStage::FragmentShader, IssouRHI::Access::ShaderResource, IssouRHI::TextureLayout::ShaderResource}),
      };

      encoder.Barrier({.textures = transitions});
    }

    std::array targets{
        IssouRHI::ColorAttachment{
            .view = renderTargetView.get(),
            .clearValue = {0.0f, 0.2f, 0.4f, 1.0f},
        },
    };
    auto passEncoder = encoder.BeginRenderPass({
        .label = "Final Compose Pass",
        .colorAttachment = targets,
        .timestampWrites = IssouRHI::TimestampWrites{
            .beginningOfPassWriteIndex = Timestamp::FinalComposeBegin,
            .endOfPassWriteIndex = Timestamp::FinalComposeEnd,
            .querySet = g_TimestampQuerySet.get(),
        },
    });

    passEncoder.SetPipeline(g_RenderPipeline.get());
    uint32_t c[] = {
        g_GBuffer.baseColor->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::Read),
        g_ShadowBuffer->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::Read),
    };
    passEncoder.PushConstants(0, SizeOfInUint(BuffersDescriptorIndices), &ctx->buffersDescriptorsIndices);
    passEncoder.PushConstants(SizeOfInUint(BuffersDescriptorIndices), 2, c);
    passEncoder.Draw(3);
    passEncoder.End();
  }

  {
    auto rtvHandle = renderTargetView->RtvDescriptorAlloc().cpuHandle;
    encoder.CommandList()->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), encoder.CommandList());
  }

  {
    std::array transitions{
        BuildTransition(renderTarget.get(), {IssouRHI::PipelineStage::None, IssouRHI::Access::None, IssouRHI::TextureLayout::Present}),
    };

    encoder.Barrier({.textures = transitions});
  }

  encoder.WriteTimestamp(g_TimestampQuerySet.get(), Timestamp::TotalEnd);
  encoder.ResolveQuerySet(g_TimestampQuerySet.get(), 0, Timestamp::Count, ctx->timestampReadBackBuffer.get(), 0);

  IssouRHI::CommandBuffer* cb[] = {encoder.Finish()};
  queue->Submit(cb);

  g_Surface->Present();
}

void Cleanup()
{
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  g_Device->GetQueue()->WaitForAll();

  // TODO: rewrite as a class so we have RAII and can forego calling Reset() manually...
  // Because all of these are static object, their dtor is called too late and ReportLiveObjects raises
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

  // FIXME: because these are static object we must call reset manually or dtor is not called before ~Device
  g_ComputePipelines[PSO::SkinningCS].reset();
  g_ComputePipelines[PSO::InstanceCullingCS].reset();
  g_ComputePipelines[PSO::FillGBufferCS].reset();

  g_RenderPipeline.reset();
  g_MeshPipeline.reset();
  g_RayTracingPipeline.reset();

  g_DrawMeshCommands.reset();
  g_UAVCounterReset.reset();

  for (auto& as : g_Scene.blasBuffers) {
    as.reset();
  }
  g_Scene.tlasBuffer.reset();

  g_ShaderTable.reset();

  g_VisibilityBuffer.reset();
  g_GBuffer.Reset();

  g_ShadowBuffer.reset();

  g_TimestampQuerySet.reset();

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
    g_DepthStencilBuffer = g_Device->CreateTexture(desc);
  }

  // Query heap
  {
    g_TimestampQuerySet = g_Device->CreateQuerySet({
        .label = "Timestamps queries",
        .type = IssouRHI::QueryType::Timestamp,
        .count = Timestamp::Count,
    });
  }

  // Setup Platform/Renderer backends
  ImGui_ImplDX12_InitInfo initInfo = {};
  initInfo.Device = g_Device->GetNativeDevice();
  initInfo.CommandQueue = g_Device->GetQueue()->GetNativeQueue();
  initInfo.NumFramesInFlight = FRAME_BUFFER_COUNT;
  initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
  // Allocating SRV descriptors (for textures) is up to the application, so we
  // provide callbacks. (current version of the backend will only allocate one
  // descriptor, future versions will need to allocate more)
  initInfo.SrvDescriptorHeap = g_Device->CbvSrvUavDescriptorHeap();
  initInfo.SrvDescriptorAllocFn =
      [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
         D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle) {
        IssouRHI::DescriptorAllocation alloc = g_Device->AllocCbvSrvUavDescriptor();
        *outCpuHandle = alloc.cpuHandle;
        *outGpuHandle = alloc.gpuHandle;
      };
  initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
                                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    IssouRHI::DescriptorAllocation alloc{};
    alloc.cpuHandle = cpuHandle;
    alloc.gpuHandle = gpuHandle;
    g_Device->FreeSrvUavDescriptor(alloc);
  };
  ImGui_ImplDX12_Init(&initInfo);

  // Mesh Shader pipeline
  {
    auto amplificationShaderBlob = ReadData(L"Meshlet.as.cso");
    auto meshShaderBlob = ReadData(L"Meshlet.ms.cso");
    auto pixelShaderBlob = ReadData(L"Meshlet.ps.cso");

    IssouRHI::ShaderModule shaderModules[] = {
        {.stage = IssouRHI::ShaderStage::Mesh, .code = meshShaderBlob.data(), .size = meshShaderBlob.size()},
        {.stage = IssouRHI::ShaderStage::Task, .code = amplificationShaderBlob.data(), .size = amplificationShaderBlob.size()},
        {.stage = IssouRHI::ShaderStage::Fragment, .code = pixelShaderBlob.data(), .size = pixelShaderBlob.size()},
    };
    IssouRHI::ColorTargetState targets[] = {{
        .format = IssouRHI::TextureFormat::R32Uint,
    }};
    g_MeshPipeline = g_Device->CreateMeshPipeline({
        .label = "Vis buffer",
        .shaders = shaderModules,
        .targets = targets,
        .depthStencil = {
            .format = IssouRHI::TextureFormat::Depth32Float,
            .depthCompare = IssouRHI::CompareFunction::Less,
            .depthWriteEnabled = true,
        },
        .primitive = {
            .cullMode = IssouRHI::CullMode::Back,
        },
    });
  }

  // Compute skinning pipeline
  {
    auto computeShaderBlob = ReadData(L"Skinning.cs.cso");

    g_ComputePipelines[PSO::SkinningCS] = g_Device->CreateComputePipeline({
        .label = "Skinning Pipeline",
        .shader = {
            .stage = IssouRHI::ShaderStage::Compute,
            .code = computeShaderBlob.data(),
            .size = computeShaderBlob.size(),
        },
    });
  }

  // Compute culling pipeline
  {
    auto computeShaderBlob = ReadData(L"InstanceCulling.cs.cso");

    g_ComputePipelines[PSO::InstanceCullingCS] = g_Device->CreateComputePipeline({
        .label = "Culling Pipeline",
        .shader = {
            .stage = IssouRHI::ShaderStage::Compute,
            .code = computeShaderBlob.data(),
            .size = computeShaderBlob.size(),
        },
    });
  }

  // Fill G-Buffer pipeline
  {
    auto computeShaderBlob = ReadData(L"FillGBuffer.cs.cso");

    g_ComputePipelines[PSO::FillGBufferCS] = g_Device->CreateComputePipeline({
        .label = "Fill G-Buffer Pipeline",
        .shader = {
            .stage = IssouRHI::ShaderStage::Compute,
            .code = computeShaderBlob.data(),
            .size = computeShaderBlob.size(),
        },
    });
  }

  // Final image composition VS/PS pipeline
  {
    auto vertexShaderBlob = ReadData(L"FullScreenTriangle.vs.cso");
    auto pixelShaderBlob = ReadData(L"FinalCompose.ps.cso");

    IssouRHI::ShaderModule shaderModules[] = {
        {.stage = IssouRHI::ShaderStage::Vertex, .code = vertexShaderBlob.data(), .size = vertexShaderBlob.size()},
        {.stage = IssouRHI::ShaderStage::Fragment, .code = pixelShaderBlob.data(), .size = pixelShaderBlob.size()},
    };
    IssouRHI::ColorTargetState targets[] = {{
        .format = IssouRHI::TextureFormat::RGBA8Unorm,
    }};

    g_RenderPipeline = g_Device->CreateRenderPipeline({
        .label = "Final Compose",
        .shaders = shaderModules,
        .targets = targets,
    });
  }

  // Ray traced shadow pipeline
  {
    auto rahitBlob = ReadData(L"RayTracing.rahit.cso");
    auto rmissBlob = ReadData(L"RayTracing.rmiss.cso");
    auto rgenBlob = ReadData(L"RayTracing.rgen.cso");

    IssouRHI::ShaderModule shaderModules[] = {
        {.stage = IssouRHI::ShaderStage::RayAnyHit, .code = rahitBlob.data(), .size = rahitBlob.size(), .entryPointName = "ShadowAnyHit"},
        {.stage = IssouRHI::ShaderStage::RayMiss, .code = rmissBlob.data(), .size = rmissBlob.size(), .entryPointName = "ShadowMiss"},
        {.stage = IssouRHI::ShaderStage::RayGen, .code = rgenBlob.data(), .size = rgenBlob.size(), .entryPointName = "ShadowRayGen"},
    };
    IssouRHI::HitGroupDesc hitGroups[] = {
        {
            .name = "MyHitGroup",
            .anyHitEntryPoint = "ShadowAnyHit",
        },
    };
    g_RayTracingPipeline = g_Device->CreateRayTracingPipelinePipeline({
        .label = "Shadow RT Pipeline",
        .shaders = shaderModules,
        .hitGroups = hitGroups,
        .maxAttributeSize = 2 * sizeof(float),  // struct BuiltInTriangleIntersectionAttributes { float2 barycentrics; };
        .maxPayloadSize = 1 * sizeof(float),    // struct ShadowPayload { float visibility; };
        .maxRecursionDepth = 1,
    });

    // Shader Table
    std::string missEntryPoints[] = {"ShadowMiss"};
    std::string hitGroupNames[] = {"MyHitGroup"};
    g_ShaderTable = g_Device->CreateShaderTable({
        .label = "Shadow RT Shader Table",
        .pipeline = g_RayTracingPipeline.get(),
        .rayGenEntryPoint = "ShadowRayGen",
        .missEntryPoints = missEntryPoints,
        .hitGroupNames = hitGroupNames,
    });
  }

  // MeshStore
  g_MeshStore.Init(g_Device);

  // Draw Meshlets commands
  {
    IssouRHI::BufferDesc desc{
        .label = "Draw Meshlets command buffer",
        .size = DRAW_MESH_CMDS_COUNTER_OFFSET + sizeof(UINT),  // counter,
        .usage = IssouRHI::BufferUsage::CopyDst | IssouRHI::BufferUsage::Indirect | IssouRHI::BufferUsage::Storage,
    };
    g_DrawMeshCommands = g_Device->CreateBuffer(desc);
  }

  // Buffer containg just a UINT (0) used to reset UAV counter.
  {
    size_t bufSiz = sizeof(UINT);

    IssouRHI::BufferDesc desc{
        .label = "UAV Reset counter",
        .size = bufSiz,
        .usage = IssouRHI::BufferUsage::MapWrite,
    };
    g_UAVCounterReset = g_Device->CreateBuffer(desc);
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
    g_VisibilityBuffer = g_Device->CreateTexture(desc);
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
    g_GBuffer.worldPosition = g_Device->CreateTexture(desc);
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
    g_GBuffer.worldNormal = g_Device->CreateTexture(desc);
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
    g_GBuffer.baseColor = g_Device->CreateTexture(desc);
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
    g_ShadowBuffer = g_Device->CreateTexture(desc);
  }

  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    auto ctx = &g_FrameContext[i];

    ctx->buffersDescriptorsIndices = g_MeshStore.BuffersDescriptorIndices(static_cast<UINT>(i));
    ctx->skinningBuffersDescriptorsIndices = g_MeshStore.SkinningBuffersDescriptorIndices(static_cast<UINT>(i));
    ctx->cullingBuffersDescriptorsIndices = {
        .InstancesBufferId = g_MeshStore.InstancesBufferId(static_cast<UINT>(i)),
        .DrawMeshCommandsBufferId = g_DrawMeshCommands->DescriptorIndex({IssouRHI::BufferAccess::ReadWrite, IssouRHI::FullBufferRange, sizeof(DrawMeshCommand), DRAW_MESH_CMDS_COUNTER_OFFSET, g_DrawMeshCommands.get()}),
    };
  }

  // frame constants buffer
  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    IssouRHI::BufferDesc desc{
        .label = std::format("Frame constants buffer {}", i),
        .size = AlignUp(sizeof(FrameConstants), 256),  // constant buffer alignment
        .usage = IssouRHI::BufferUsage::MapWrite,
    };
    g_FrameContext[i].frameConstantBuffer = g_Device->CreateBuffer(desc);
  }

  // timestamp readback buffer
  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    IssouRHI::BufferDesc desc{
        .label = std::format("Timestamp Readback Buffer {}", i),
        .size = sizeof(UINT64) * Timestamp::Count,
        .usage = IssouRHI::BufferUsage::MapRead,
    };
    g_FrameContext[i].timestampReadBackBuffer = g_Device->CreateBuffer(desc);
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
    if (it == std::end(g_Scene.meshInstanceMap)) {  // first time seeing this mesh
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
      } else /* if not skinned */ {
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

      if (!mesh->Skinned()) {
        g_Scene.uniqueMeshInstances.push_back(mi);  // TODO: only non skinned mesh for now
      }
    } else /* if an instance for this mesh already exists */ {
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
    return it->second->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::Read);
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
  auto tex = g_Device->CreateTexture(textureDesc);

  std::vector<D3D12_SUBRESOURCE_DATA> subresources;
  CHECK_HR(PrepareUpload(g_Device->GetNativeDevice(), image.GetImages(), image.GetImageCount(), metadata, subresources));

  tex->WriteToSubresource(subresources.data(), static_cast<UINT>(subresources.size()));

  g_Textures[filename] = tex;

  return tex->CreateView()->DescriptorIndex(IssouRHI::TextureAccess::Read);
}
}  // namespace Renderer
