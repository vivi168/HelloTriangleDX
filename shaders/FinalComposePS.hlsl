#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

cbuffer PerDrawConstants : register(b0)
{
  uint GBufferBaseColorId;
}

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

float4 main(float4 position : SV_Position) : SV_Target
{
  Texture2D<float4> tex = ResourceDescriptorHeap[GBufferBaseColorId];
  float4 color = tex.Load(int3(position.xy, 0));

  if (all(color == 0)) discard;

  return color;
}
