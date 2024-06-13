#include "common.hlsli"

Texture2D t0 : register(t0);
SamplerState s0 : register(s0);

float4 main(VS_OUTPUT input) : SV_TARGET
{
  float4 normalColor;
  
  if (input.normal.y > 0.25)
    normalColor = float4(0, 0, 1, 1);
  else if (input.normal.y < -0.25)
    normalColor = float4(1, 0, 0, 1);
  else
    normalColor = float4(0, 1, 0, 1);
  
  float4 color2 = t0.Sample(s0, input.texCoord) * normalColor;
    
  if (color2.a < 0.01)
    discard;

  return color2;
}
