struct MS_OUTPUT
{
  float4 pos : SV_POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  uint meshletIndex : COLOR0;
};


float4 main(MS_OUTPUT input) : SV_TARGET
{
  uint meshletIndex = input.meshletIndex;
  float3 diffuseColor = float3(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8);
  
  return float4(diffuseColor, 1.0f);
}
