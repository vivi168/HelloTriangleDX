#include "Shared.h"

struct ShadowPayload
{
  float visibility;
};

cbuffer PerDrawConstants : register(b0)
{
  uint GBufferWorldPosId;
  uint ShadowBufferId;
  uint TlasId;
}

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

[shader("raygeneration")]
void ShadowRayGen()
{
  uint2 launchIdx = DispatchRaysIndex().xy;

  Texture2D<float4> positions = ResourceDescriptorHeap[GBufferWorldPosId];
  float4 worldPos = positions.Load(int3(launchIdx, 0));

  RaytracingAccelerationStructure Scene = ResourceDescriptorHeap[TlasId];
  uint flags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
  RayDesc ray = { worldPos.xyz, 1.0e-2f, normalize(-g_FrameConstants.SunDirection), 1.0e3f };
  ShadowPayload payload = { 0.0f };

  TraceRay(Scene, flags, 0xff, 0, 1, 0, ray, payload);

  RWTexture2D<float> RenderTarget = ResourceDescriptorHeap[ShadowBufferId];

  RenderTarget[launchIdx] = payload.visibility;
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  // TODO: sample textures to see if we hit cutout part of texture (eg: fence, leaves, etc)
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
  payload.visibility = 1.0f;
}
