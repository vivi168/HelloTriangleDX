#include "common.hlsli"

cbuffer ObjectCb : register(b1)
{
  float4x4 WorldViewProj;
};

VS_OUTPUT main(VS_INPUT_TERRAIN input)
{
  VS_OUTPUT output;
  output.pos = mul(float4(input.pos, 1.0f), WorldViewProj);

  output.color = float4(1.0f, 0.0f, 1.0f, 1.0f);
  output.texCoord = input.texCoord;
  return output;
}
