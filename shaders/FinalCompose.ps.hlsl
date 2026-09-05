#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

ConstantBuffer<FinalComposePassArgs> g_Args : register(b0);

float4 main(float4 position : SV_Position) : SV_Target
{
  Texture2D<float4> baseColor = ResourceDescriptorHeap[g_Args.GBufferBaseColorId];
  Texture2D<float> shadow = ResourceDescriptorHeap[g_Args.ShadowBufferId];

  float4 color = baseColor.Load(int3(position.xy, 0));
  float lit = shadow.Load(int3(position.xy, 0));

  if (all(color == 0)) discard;

  float shadowIntensity = 0.6f;
  float lightingFactor = lerp(1.0f, lit, shadowIntensity);

  return color * lightingFactor;
}
