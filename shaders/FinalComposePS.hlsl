#include "Shared.h"
#include "VisibilityBufferCommon.hlsli"

cbuffer PerDrawConstants : register(b0)
{
  uint VisibilityBufferId;
}

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

SamplerState s0 : register(s0);

// TODO: TMP just to visualize it's working
float4 main(float4 position : SV_Position) : SV_Target
{
  Texture2D<uint> tex = ResourceDescriptorHeap[VisibilityBufferId];
  uint value = tex.Load(int3(position.xy, 0));

  if (value == 0) discard;

  Visibility vis = UnpackVisibility(value);

  StructuredBuffer<MeshletData> meshlets = ResourceDescriptorHeap[g_DescIds.meshletsBufferId];
  MeshletData m = meshlets[vis.meshletIndex];
  uint instanceId = m.instanceIndex + 1;

  uint h = instanceId * 2654435761;
  uint r = (h >> 0) & 0xff;
  uint g = (h >> 8) & 0xff;
  uint b = (h >> 16) & 0xff;
  float4 color = float4(float(r), float(g), float(b), 255.0f) / 255.0f;

  return color;
}
