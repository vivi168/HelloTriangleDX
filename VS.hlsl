#include "common.hlsli"

cbuffer ObjectCb : register(b1)
{
  float4x4 WorldViewProj;
};

VS_OUTPUT main(VS_INPUT input)
{
  VS_OUTPUT output;
  output.pos = mul(WorldViewProj, float4(input.pos, 1.0f));

  output.color = input.color;
  output.texCoord = input.texCoord;
  output.normal = input.normal;
  return output;
}
