#include "Shared.h"

struct ShadowPayload
{
  float visibility;
};

ConstantBuffer<ShadowPassArgs> g_Args : register(b0);

[shader("raygeneration")]
void ShadowRayGen()
{
  uint2 launchIdx = DispatchRaysIndex().xy;

  Texture2D<float4> positions = ResourceDescriptorHeap[g_Args.GBufferWorldPosId];
  float4 worldPos = positions.Load(int3(launchIdx, 0));

  RaytracingAccelerationStructure Scene = ResourceDescriptorHeap[g_Args.TlasId];
  uint flags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
  ConstantBuffer<FrameConstants> g_FrameConstants = ResourceDescriptorHeap[g_Args.FrameConstantsIndex];
  RayDesc ray = { worldPos.xyz, 1.0e-2f, normalize(-g_FrameConstants.SunDirection), 1.0e3f };
  ShadowPayload payload = { 0.0f };

  TraceRay(Scene, flags, 0xff, 0, 1, 0, ray, payload);

  RWTexture2D<float> RenderTarget = ResourceDescriptorHeap[g_Args.ShadowBufferId];

  RenderTarget[launchIdx] = payload.visibility;
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  // TODO: sample textures to see if we hit cutout part of texture (eg: fence, leaves, etc)
  // ^ use OMM instead in 2026 loul
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
  payload.visibility = 1.0f;
}
