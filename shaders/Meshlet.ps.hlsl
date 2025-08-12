#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

SamplerState s1 : register(s1);

uint main(VertexOut v, uint primitiveIndex : SV_PrimitiveID) : SV_Target
{
#define ENABLE_TEXTURE_ALPHA_TEST
#ifdef ENABLE_TEXTURE_ALPHA_TEST
  Texture2D baseColorTex = ResourceDescriptorHeap[NonUniformResourceIndex(v.textureIndex)];
  float4 baseColor = baseColorTex.Sample(s1, v.uv);

  if (baseColor.w < 0.5) discard;
#endif
  return PackVisibility(v.meshletIndex, primitiveIndex);
}
