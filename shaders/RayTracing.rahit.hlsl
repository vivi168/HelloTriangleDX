struct ShadowPayload
{
  float visibility;
};

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
  // TODO: sample textures to see if we hit cutout part of texture (eg: fence, leaves, etc)
  // ^ use OMM instead in 2026 loul
}
