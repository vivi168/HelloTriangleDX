cbuffer FrameConstants : register(b0)
{
  float time;
  float deltaTime;
};

cbuffer MeshletConstants : register(b3)
{
  uint instanceBufferOffset;
};

cbuffer BuffersDescriptorIndices : register(b4)
{
  uint vertexPositionsBufferId;
  uint vertexNormalsBufferId;
  // TODO: tangents
  uint vertexUVsBufferId;
  // TODO: blend

  uint meshletsBufferId;
  uint visibleMeshletsBufferId;
  uint meshletUniqueIndicesBufferId;
  uint meshletsPrimitivesBufferId;
  uint meshletMaterialsBufferId;

  // TODO: materials
  uint instancesBufferId;
};

struct MeshInstance {
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
};

SamplerState s0 : register(s0);

float4 main(MS_OUTPUT input, uint primitiveId : SV_PrimitiveID) : SV_TARGET
{
  StructuredBuffer<MeshInstance> meshInstances = ResourceDescriptorHeap[instancesBufferId];
  MeshInstance mi = meshInstances[instanceBufferOffset];

  uint meshletIndex = input.meshletIndex;

  StructuredBuffer<uint> MaterialIndices = ResourceDescriptorHeap[meshletMaterialsBufferId];
  uint materialId = MaterialIndices[meshletIndex];

  Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(materialId)];
  float4 diffuseColor = tex.Sample(s0, input.texCoord);

  if (diffuseColor.a == 0)
    discard;
  
  //float4 meshletColor = float4(
  //          float(meshletIndex & 1),
  //          float(meshletIndex & 3) / 4,
  //          float(meshletIndex & 7) / 8,
  //          1.0f);

  //float4 primitiveColor = float4(
  //          float(primitiveId & 1),
  //          float(primitiveId & 3) / 4,
  //          float(primitiveId & 7) / 8,
  //          1.0f);

  return diffuseColor;
}
