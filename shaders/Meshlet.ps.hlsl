#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

ConstantBuffer<VisibilityBufferPassArgs> g_Args : register(b0);

uint main(VertexOut v, uint primitiveIndex : SV_PrimitiveID) : SV_Target
{
// TODO: two pipelines?
#define ENABLE_TEXTURE_ALPHA_TEST
#ifdef ENABLE_TEXTURE_ALPHA_TEST
  Texture2D baseColorTex = ResourceDescriptorHeap[NonUniformResourceIndex(v.textureIndex)];
  SamplerState materialSampler = SamplerDescriptorHeap[g_Args.MaterialSamplerId];
  float4 baseColor = baseColorTex.Sample(materialSampler, v.uv);

  if (baseColor.w < 0.5) discard;
#endif
  return PackVisibility(v.meshletIndex, primitiveIndex);
}
