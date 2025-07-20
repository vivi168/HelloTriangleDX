#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

cbuffer PerDrawConstants : register(b0)
{
  uint VisibilityBufferId;
}

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

SamplerState s0 : register(s0);
SamplerState s1 : register(s1);

struct Vertex {
  float4 posCS;
  float2 uv;
};

struct BarycentricDeriv {
  float3 m_lambda;
  float3 m_ddx;
  float3 m_ddy;
};

Vertex GetVertexAttributes(MeshInstanceData mi, uint vertexIndex)
{
  StructuredBuffer<float3> positions = ResourceDescriptorHeap[g_DescIds.vertexPositionsBufferId];
  float3 position = positions[mi.firstPosition + vertexIndex];

  StructuredBuffer<float2> uvs = ResourceDescriptorHeap[g_DescIds.vertexUVsBufferId];
  float2 uv = uvs[mi.firstUV + vertexIndex];

  Vertex vout;
  vout.posCS = mul(float4(position, 1.0f), mi.worldViewProj);
  vout.uv = uv;

  return vout;
}

// http://filmicworlds.com/blog/visibility-buffer-rendering-with-material-graphs/
BarycentricDeriv CalcFullBary(float4 p0, float4 p1, float4 p2, float2 pixelNdc, float2 ScreenSize)
{
  BarycentricDeriv ret = (BarycentricDeriv)0;

  float3 invW = rcp(float3(p0.w, p1.w, p2.w));

  float2 ndc0 = p0.xy * invW.x;
  float2 ndc1 = p1.xy * invW.y;
  float2 ndc2 = p2.xy * invW.z;

  float invDet = rcp(determinant(float2x2(ndc2 - ndc1, ndc0 - ndc1)));
  ret.m_ddx = float3(ndc1.y - ndc2.y, ndc2.y - ndc0.y, ndc0.y - ndc1.y) * invDet * invW;
  ret.m_ddy = float3(ndc2.x - ndc1.x, ndc0.x - ndc2.x, ndc1.x - ndc0.x) * invDet * invW;
  float ddxSum = dot(ret.m_ddx, float3(1, 1, 1));
  float ddySum = dot(ret.m_ddy, float3(1, 1, 1));

  float2 deltaVec = pixelNdc - ndc0;
  float interpInvW = invW.x + deltaVec.x * ddxSum + deltaVec.y * ddySum;
  float interpW = rcp(interpInvW);

  ret.m_lambda.x = interpW * (invW[0] + deltaVec.x * ret.m_ddx.x + deltaVec.y * ret.m_ddy.x);
  ret.m_lambda.y = interpW * (0.0f + deltaVec.x * ret.m_ddx.y + deltaVec.y * ret.m_ddy.y);
  ret.m_lambda.z = interpW * (0.0f + deltaVec.x * ret.m_ddx.z + deltaVec.y * ret.m_ddy.z);

  ret.m_ddx *= (2.0f / ScreenSize.x);
  ret.m_ddy *= (2.0f / ScreenSize.y);
  ddxSum *= (2.0f / ScreenSize.x);
  ddySum *= (2.0f / ScreenSize.y);

  ret.m_ddy *= -1.0f;
  ddySum *= -1.0f;

  float interpW_ddx = 1.0f / (interpInvW + ddxSum);
  float interpW_ddy = 1.0f / (interpInvW + ddySum);

  ret.m_ddx = interpW_ddx * (ret.m_lambda * interpInvW + ret.m_ddx) - ret.m_lambda;
  ret.m_ddy = interpW_ddy * (ret.m_lambda * interpInvW + ret.m_ddy) - ret.m_lambda;

  return ret;
}

float3 InterpolateWithDeriv(BarycentricDeriv deriv, float v0, float v1, float v2)
{
  float3 mergedV = float3(v0, v1, v2);
  float3 ret;
  ret.x = dot(mergedV, deriv.m_lambda);
  ret.y = dot(mergedV, deriv.m_ddx);
  ret.z = dot(mergedV, deriv.m_ddy);
  return ret;
}

float4 main(float4 position : SV_Position) : SV_Target
{
  Texture2D<uint> tex = ResourceDescriptorHeap[VisibilityBufferId];
  uint value = tex.Load(int3(position.xy, 0));

  if (value == 0) discard;

  Visibility vis = UnpackVisibility(value);

  MeshletData m = GetMeshletData(g_DescIds, vis.meshletIndex);
  MeshInstanceData mi = GetInstanceData(g_DescIds, m.instanceIndex);

  uint3 tri = GetPrimitive(g_DescIds, mi.firstPrimitive + m.firstPrim + vis.primitiveIndex);

  uint i0 = GetVertexIndex(g_DescIds, mi.firstVertIndex + m.firstVert + tri.x);
  Vertex v0 = GetVertexAttributes(mi, i0);

  uint i1 = GetVertexIndex(g_DescIds, mi.firstVertIndex + m.firstVert + tri.y);
  Vertex v1 = GetVertexAttributes(mi, i1);

  uint i2 = GetVertexIndex(g_DescIds, mi.firstVertIndex + m.firstVert + tri.z);
  Vertex v2 = GetVertexAttributes(mi, i2);

  // ------

  float2 pixelCenter = position.xy + 0.5;
  float2 pixelNdc = float2((pixelCenter.x / g_FrameConstants.ScreenSize.x) * 2.0f - 1.0f, 1.0f - (pixelCenter.y / g_FrameConstants.ScreenSize.y) * 2.0f);
  BarycentricDeriv barycentrics = CalcFullBary(v0.posCS, v1.posCS, v2.posCS, pixelNdc, g_FrameConstants.ScreenSize);

  float3 uvx = InterpolateWithDeriv(barycentrics, v0.uv.x, v1.uv.x, v2.uv.x);
  float3 uvy = InterpolateWithDeriv(barycentrics, v0.uv.y, v1.uv.y, v2.uv.y);

  float2 uv = float2(uvx.x, uvy.x);
  float2 uv_ddx = float2(uvx.y, uvy.y);
  float2 uv_ddy = float2(uvx.z, uvy.z);

  StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[g_DescIds.materialsBufferId];
  MaterialData material = materials[m.materialIndex];

  Texture2D baseColor = ResourceDescriptorHeap[NonUniformResourceIndex(material.baseColorId)];
  float4 color = baseColor.SampleGrad(s1, uv, uv_ddx, uv_ddy);

  return color;
}
