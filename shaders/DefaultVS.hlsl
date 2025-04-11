#include "Common.hlsli"

cbuffer ObjectCb : register(b1)
{
  float4x4 WorldViewProj;
  float4x4 WorldMatrix;
  float4x4 NormalMatrix;
};

StructuredBuffer<float4x4> BoneMatrices : register(t0);

VS_OUTPUT main(VS_INPUT input)
{
  VS_OUTPUT output;


#ifdef SKINNED
  float4 pos = float4(input.pos, 1.0f);
  float4 skinnedPos = mul(pos, BoneMatrices[input.boneIndices.x]) * input.boneWeights.x +
                      mul(pos, BoneMatrices[input.boneIndices.y]) * input.boneWeights.y +
                      mul(pos, BoneMatrices[input.boneIndices.z]) * input.boneWeights.z +
                      mul(pos, BoneMatrices[input.boneIndices.w]) * input.boneWeights.w;
  output.pos = mul(skinnedPos, WorldViewProj);
#else
  output.pos = mul(float4(input.pos, 1.0f), WorldViewProj);
#endif

  output.color = input.color;
  output.texCoord = input.texCoord;
  float3 norm = mul(float4(input.normal, 1.0f), NormalMatrix).xyz;
  output.normal = normalize(norm);
  return output;
}
