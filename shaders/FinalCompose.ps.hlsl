#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

cbuffer PerDrawConstants : register(b0)
{
  uint GBufferBaseColorId;
  uint ShadowBufferId;
}

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

float4 main(float4 position : SV_Position) : SV_Target
{
  Texture2D<float4> baseColor = ResourceDescriptorHeap[GBufferBaseColorId];
  Texture2D<float> shadow = ResourceDescriptorHeap[ShadowBufferId];

  float4 color = baseColor.Load(int3(position.xy, 0));
  float lit = shadow.Load(int3(position.xy, 0));

  if (all(color == 0)) discard;

  float shadowIntensity = 0.6f;
  float lightingFactor = lerp(1.0f, lit, shadowIntensity);

  return color * lightingFactor;
}
