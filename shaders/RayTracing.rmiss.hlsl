struct ShadowPayload
{
  float visibility;
};

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
  payload.visibility = 1.0f;
}
