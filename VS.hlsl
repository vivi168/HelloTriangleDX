#include "common.hlsli"

cbuffer ObjectCb : register(b1)
{
  float4x4 WorldViewProj;
  float4x4 WorldMatrix;
  float4x4 NormalMatrix;
};

VS_OUTPUT main(VS_INPUT input)
{
  VS_OUTPUT output;
  output.pos = mul(WorldViewProj, float4(input.pos, 1.0f));

  output.color = input.color;
  output.texCoord = input.texCoord;
  float3 norm = mul(NormalMatrix, float4(input.normal, 1.0f));
  output.normal = normalize(norm);
  return output;
}
