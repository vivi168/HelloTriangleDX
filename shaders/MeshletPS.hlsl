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
  MeshInstance mi = meshInstances[InstanceIndex];

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
#elif defined PRIMITIVE_COLOR
  float4 color = float4(float(primitiveId & 1),
                        float(primitiveId & 3) / 4,
                        float(primitiveId & 7) / 8,
                        1.0f);
#else
  uint h = InstanceIndex * 2654435761;
  uint r = (h >> 0) & 0xff;
  uint g = (h >> 8) & 0xff;
  uint b = (h >> 16) & 0xff;
  float4 color = float4(float(r), float(g), float(b), 255.0f) / 255.0f;
#endif
  return color;
}
