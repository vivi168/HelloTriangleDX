#include "MeshletCommon.hlsli"

#define BASE_COLOR 1

struct Material
{
  uint baseColorId;
  uint metallicRoughnessId;
  uint normalMapId;
  uint pad;
};

SamplerState s0 : register(s0);

float4 main(MS_OUTPUT input, uint primitiveId : SV_PrimitiveID) : SV_TARGET
{
  StructuredBuffer<MeshInstance> meshInstances = ResourceDescriptorHeap[instancesBufferId];
  MeshInstance mi = meshInstances[instanceBufferOffset];

  uint meshletIndex = input.meshletIndex;

 #ifdef BASE_COLOR
  uint materialIndex = input.materialIndex;
  StructuredBuffer<Material> materials = ResourceDescriptorHeap[materialsBufferId];
  Material material = materials[materialIndex];

  Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(material.baseColorId)];
  float4 color = tex.Sample(s0, input.texCoord);

  if (color.a == 0)
    discard;
#elif defined MESHLET_COLOR
  float4 color = float4(float(meshletIndex & 1),
                        float(meshletIndex & 3) / 4,
                        float(meshletIndex & 7) / 8,
                        1.0f);
#else
  float4 color = float4(float(primitiveId & 1),
                        float(primitiveId & 3) / 4,
                        float(primitiveId & 7) / 8,
                        1.0f);
#endif
  return color;
}
