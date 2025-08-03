#include "Shared.h"

struct ShadowPayload
{
  float visibility;
};

cbuffer PerDrawConstants : register(b0)
{
  uint GBufferWorldPosId;
  uint GBufferWorldNormId;
  uint ShadowBufferId;
  uint TlasId;
}

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

[shader("raygeneration")]
void ShadowRayGen()
{
  uint2 launchIdx = DispatchRaysIndex().xy;
  uint randSeed = 0;

  Texture2D<float4> positions = ResourceDescriptorHeap[GBufferWorldPosId];
  float4 worldPos = positions.Load(int3(launchIdx.xy, 0));

  RayDesc ray = { worldPos.xyz, 1.0e-4f, normalize(-g_FrameConstants.SunDirection), 1.0e4f };

  uint flags = RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;

  RaytracingAccelerationStructure Scene = ResourceDescriptorHeap[TlasId];

  ShadowPayload payload = { 0.0f };

  TraceRay(Scene, flags, 0xff, 0, 1, 0, ray, payload);

  RWTexture2D<float> RenderTarget = ResourceDescriptorHeap[ShadowBufferId];

  RenderTarget[DispatchRaysIndex().xy] = payload.visibility;
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  // TODO: sample textures to see if we hit a leave etc
  AcceptHitAndEndSearch();
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
  payload.visibility = 1.0f;
}
