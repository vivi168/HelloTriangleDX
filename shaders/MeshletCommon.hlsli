cbuffer MeshletConstants : register(b0)
{
  uint instanceBufferOffset;
  uint numMeshlets;
  uint numInstances;
};

cbuffer FrameConstants : register(b1)
{
  float time;
  float deltaTime;
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

  uint positionsBufferOffset;
  uint normalsBufferOffset;
  // TODO: tangents
  uint uvsBufferOffset;

  uint meshletBufferOffset;
  uint indexBufferOffset;
  uint primBufferOffset;
  
  uint2 pad;
};

struct MS_OUTPUT
{
  float4 pos : SV_POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  uint meshletIndex : COLOR0;
  uint materialIndex : COLOR1;
};
