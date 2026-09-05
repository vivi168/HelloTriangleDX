#include "Shared.h"

ConstantBuffer<SkinningPassArgs> g_Args : register(b0);

float4 UnpackBoneWeights(uint bw)
{
  float w1 = float(bw & 0xff) / 255.0;
  float w2 = float((bw >> 8) & 0xff) / 255.0;
  float w3 = float((bw >> 16) & 0xff) / 255.0;
  float w4 = float((bw >> 24) & 0xff) / 255.0;

  return float4(w1, w2, w3, w4);
}

uint4 UnpackBoneIndices(uint bi)
{
  uint b1 = bi & 0xff;
  uint b2 = (bi >> 8) & 0xff;
  uint b3 = (bi >> 16) & 0xff;
  uint b4 = (bi >> 24) & 0xff;

  return uint4(b1, b2, b3, b4);
}

[numthreads(COMPUTE_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID)
{
  if (dtid >= g_Args.constants.numVertices) return;

  uint basePositionIdx = dtid + g_Args.constants.firstPosition;
  uint outPositionIdx = dtid + g_Args.constants.firstSkinnedPosition;
  uint bwiIdx = dtid + g_Args.constants.firstBWI;

  RWStructuredBuffer<float3> positions = ResourceDescriptorHeap[g_Args.buffers.vertexPositionsBufferId];
  StructuredBuffer<uint2> bwis = ResourceDescriptorHeap[g_Args.buffers.vertexBlendWeightsAndIndicesBufferId];
  StructuredBuffer<float4x4> BoneMatrices = ResourceDescriptorHeap[g_Args.buffers.boneMatricesBufferId];

  float3 inPos = positions[basePositionIdx];
  uint2 bwi = bwis[bwiIdx];
  float4 boneWeights = UnpackBoneWeights(bwi.x);
  uint4 boneIndices = UnpackBoneIndices(bwi.y) + g_Args.constants.firstBoneMatrix;

  float4 pos = float4(inPos, 1.0f);
  float4 skinnedPos = mul(pos, BoneMatrices[boneIndices.x]) * boneWeights.x +
                      mul(pos, BoneMatrices[boneIndices.y]) * boneWeights.y +
                      mul(pos, BoneMatrices[boneIndices.z]) * boneWeights.z +
                      mul(pos, BoneMatrices[boneIndices.w]) * boneWeights.w;

  // TODO: need to also transform normals

  positions[outPositionIdx] = skinnedPos.xyz;
}
