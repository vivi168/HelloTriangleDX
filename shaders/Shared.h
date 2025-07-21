#ifndef SHARED_H_INCLUDED
#define SHARED_H_INCLUDED

#define WAVE_GROUP_SIZE 32
#define COMPUTE_GROUP_SIZE 64
#define MESHLET_MAX_PRIM 124
#define MESHLET_MAX_VERT 64
#define FILL_GBUFFER_GROUP_SIZE_X 16
#define FILL_GBUFFER_GROUP_SIZE_Y 16

#ifdef __cplusplus
using hlsl_float3x3 = DirectX::XMFLOAT3X3;
using hlsl_float4x4 = DirectX::XMFLOAT4X4;
using hlsl_float4 = DirectX::XMFLOAT4;
using hlsl_float3 = DirectX::XMFLOAT3;
using hlsl_float2 = DirectX::XMFLOAT2;
using hlsl_uint = UINT;
using hlsl_bounding_sphere = DirectX::BoundingSphere;
using hlsl_byte4 = DirectX::PackedVector::XMUBYTEN4;
#else
#define hlsl_float4x4 float4x4
#define hlsl_float3x3 float3x3
#define hlsl_float4 float4
#define hlsl_float3 float3
#define hlsl_float2 float2
#define hlsl_uint uint
#define hlsl_bounding_sphere float4
#define hlsl_byte4 uint
#endif

struct FrameConstants {
  float Time;
  hlsl_float3 CameraWS;
  hlsl_float4 FrustumPlanes[6];
  hlsl_float2 ScreenSize;
};

struct BuffersDescriptorIndices {
  hlsl_uint vertexPositionsBufferId;
  hlsl_uint vertexNormalsBufferId;
  hlsl_uint vertexTangentsBufferId;
  hlsl_uint vertexUVsBufferId;

  hlsl_uint meshletsBufferId;
  hlsl_uint meshletVertIndicesBufferId;
  hlsl_uint meshletsPrimitivesBufferId;

  hlsl_uint materialsBufferId;
  hlsl_uint instancesBufferId;
};

struct SkinningBuffersDescriptorIndices {
  hlsl_uint vertexPositionsBufferId;
  hlsl_uint vertexNormalsBufferId;
  hlsl_uint vertexTangentsBufferId;
  hlsl_uint vertexBlendWeightsAndIndicesBufferId;
  hlsl_uint boneMatricesBufferId;
};

struct SkinningPerDispatchConstants {
  hlsl_uint firstPosition;
  hlsl_uint firstSkinnedPosition;
  hlsl_uint firstNormal;
  hlsl_uint firstSkinnedNormal;
  hlsl_uint firstTangent;
  hlsl_uint firstSkinnedTangent;
  hlsl_uint firstBWI;
  hlsl_uint firstBoneMatrix;
  hlsl_uint numVertices;
};

struct CullingBuffersDescriptorIndices {
  hlsl_uint InstancesBufferId;
  hlsl_uint DrawMeshCommandsBufferId;
};

struct FillGBufferPerDispatchConstants {
  hlsl_uint VisibilityBufferId;
  hlsl_uint WorldPositionId;
  hlsl_uint WorldNormalId;
  hlsl_uint BaseColorId;
};

#ifdef __cplusplus
static constexpr size_t BuffersDescriptorIndicesNumValues = sizeof(BuffersDescriptorIndices) / sizeof(hlsl_uint);
static constexpr size_t SkinningBuffersDescriptorIndicesNumValues = sizeof(SkinningBuffersDescriptorIndices) / sizeof(hlsl_uint);
static constexpr size_t CullingBuffersDescriptorIndicesNumValues = sizeof(CullingBuffersDescriptorIndices) / sizeof(hlsl_uint);
static constexpr size_t SkinningPerDispatchConstantsNumValues = sizeof(SkinningPerDispatchConstants) / sizeof(hlsl_uint);
#endif

struct MaterialData {
  hlsl_uint baseColorId;
  hlsl_uint metallicRoughnessId;
  hlsl_uint normalMapId;

  // TODO: Add double sided true/false?
  hlsl_uint pad;
};

struct MeshletData {
  hlsl_uint numVerts;
  hlsl_uint firstVert;
  hlsl_uint numPrims;
  hlsl_uint firstPrim;

  hlsl_uint instanceIndex;
  hlsl_uint materialIndex;

  hlsl_bounding_sphere boundingSphere;  // xyz = center, w = radius
  hlsl_byte4 normalCone;                // xyz = axis, w = -cos(a + 90)
  float apexOffset;                     // apex = center - axis * offset
};

struct MeshInstanceData {
  // TODO: keep this separate to minimize data transfer?
  hlsl_float4x4 worldViewProj;
  hlsl_float4x4 worldMatrix;
  hlsl_float3x3 normalMatrix;
  hlsl_float4 boundingSphere;
  float scale;

  // Geometry. this doesn't change. split into two GpuBuffer?
  // if instead we add a uint geometryId to MeshInstance,
  // we could also do LODing. uint geometryId[MAX_LOD];
  hlsl_uint firstPosition;
  hlsl_uint firstNormal;
  hlsl_uint firstTangent;
  hlsl_uint firstUV;

  hlsl_uint firstMeshlet;
  hlsl_uint firstVertIndex;
  hlsl_uint firstPrimitive;

  hlsl_uint numMeshlets;

  // TODO: add skinned true/false?
  hlsl_uint _pad[2];
};

#ifdef __cplusplus
static_assert(sizeof(MeshletData) % 16 == 0);
static_assert(sizeof(MeshInstanceData) % 16 == 0);
#endif

#endif
