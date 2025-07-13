// should keep these 3 aligned with C++ side...
#define WAVE_GROUP_SIZE 32
#define MESHLET_MAX_PRIM 124
#define MESHLET_MAX_VERT 64

cbuffer MeshletConstants : register(b0)
{
  uint InstanceIndex;
};

cbuffer FrameConstants : register(b1)
{
  float Time;
  float3 CameraWS;
  float4 FrustumPlanes[6];
  float2 ScreenSize;
};

cbuffer BuffersDescriptorIndices : register(b2)
{
  uint vertexPositionsBufferId;
  uint vertexNormalsBufferId;
  // TODO: tangents
  uint vertexUVsBufferId;

  uint meshletsBufferId;
  uint visibleMeshletsBufferId;
  uint meshletUniqueIndicesBufferId;
  uint meshletsPrimitivesBufferId;

  uint materialsBufferId;
  uint instancesBufferId;
};

struct MeshInstance
{
  float4x4 WorldViewProj;
  float4x4 WorldMatrix;
  float4x4 NormalMatrix;
  float4 BoundingSphere;
  float scale;

  uint positionsBufferOffset;
  uint normalsBufferOffset;
  // TODO: tangents
  uint uvsBufferOffset;

  uint meshletBufferOffset;
  uint indexBufferOffset;
  uint primBufferOffset;

  uint numMeshlets;
};

struct Meshlet
{
  uint VertCount;
  uint VertOffset;
  uint PrimCount;
  uint PrimOffset;

  uint materialIndex;

  float4 boundingSphere;
  uint normalCone;
  float apexOffset;
  uint pad;
};

struct ASPayload
{
    uint MeshletIndices[WAVE_GROUP_SIZE];
};

struct MS_OUTPUT
{
  float4 posCS : SV_POSITION;
  float4 posWS : POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  uint meshletIndex : COLOR0;
  uint materialIndex : COLOR1;
};
