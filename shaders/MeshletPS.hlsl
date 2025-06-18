#include "MeshletCommon.hlsli"

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
  //return meshletColor;

  //float4 primitiveColor = float4(
  //          float(primitiveId & 1),
  //          float(primitiveId & 3) / 4,
  //          float(primitiveId & 7) / 8,
  //          1.0f);

  return diffuseColor;
}
