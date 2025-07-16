static const float2 positions[3] = {
  float2(-1, -1),
  float2(-1, 3),
  float2(3, -1)
};

float4 main(uint vertexId : SV_VertexID) : SV_Position
{
  return float4(positions[vertexId], 0, 1);
}
