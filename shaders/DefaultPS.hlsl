#include "Common.hlsli"

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

  Texture2D tex = ResourceDescriptorHeap[diffuseIndex];
  float4 color2 = tex.Sample(s0, input.texCoord) * normalColor;
    
  if (color2.a == 0)
    discard;

  return color2;
}
