#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

ConstantBuffer<FillGBufferPerDispatchConstants> g_PerDispatchConstants : register(b0);

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

SamplerState s1 : register(s1);

struct Vertex {
  float4 posCS;
  float4 posWS;
  float3 normalWS;
  float3 tangentWS;
  float bitangentSign;
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

  StructuredBuffer<float3> normals = ResourceDescriptorHeap[g_DescIds.vertexNormalsBufferId];
  float3 normal = normals[mi.firstNormal + vertexIndex];

  StructuredBuffer<float4> tangents = ResourceDescriptorHeap[g_DescIds.vertexTangentsBufferId];
  float4 tangent = tangents[mi.firstTangent + vertexIndex];

  StructuredBuffer<float2> uvs = ResourceDescriptorHeap[g_DescIds.vertexUVsBufferId];
  float2 uv = uvs[mi.firstUV + vertexIndex];

  Vertex vout;
  vout.posCS = mul(float4(position, 1.0f), mi.worldViewProj);
  vout.posWS = mul(float4(position, 1.0f), mi.worldMatrix);
  float3 normalWS = mul(normal, mi.normalMatrix);
  float3 tangentWS = mul(tangent.xyz, mi.normalMatrix);
  vout.normalWS = normalize(normalWS);
  vout.tangentWS = normalize(tangentWS);
  vout.bitangentSign = tangent.w;
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

float3 Interpolate(float3 lambda, float3 v0, float3 v1, float3 v2)
{
 return v0 * lambda.x + v1 * lambda.y + v2 * lambda.z;
}

[NumThreads(FILL_GBUFFER_GROUP_SIZE_X, FILL_GBUFFER_GROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
  uint2 position = dtid.xy;

  if (any(position >= uint2(g_FrameConstants.ScreenSize))) {
    return;
  }

  Texture2D<uint> tex = ResourceDescriptorHeap[g_PerDispatchConstants.VisibilityBufferId];
  uint value = tex.Load(int3(position, 0));

  if (value == 0) return;

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

  // Compute Barycentrics

  float2 pixelCenter = position + 0.5;
  float2 pixelNdc = float2((pixelCenter.x / g_FrameConstants.ScreenSize.x) * 2.0f - 1.0f, 1.0f - (pixelCenter.y / g_FrameConstants.ScreenSize.y) * 2.0f);
  BarycentricDeriv barycentrics = CalcFullBary(v0.posCS, v1.posCS, v2.posCS, pixelNdc, g_FrameConstants.ScreenSize);

  // Base Color

  float3 uvx = InterpolateWithDeriv(barycentrics, v0.uv.x, v1.uv.x, v2.uv.x);
  float3 uvy = InterpolateWithDeriv(barycentrics, v0.uv.y, v1.uv.y, v2.uv.y);

  float2 uv = float2(uvx.x, uvy.x);
  float2 uv_ddx = float2(uvx.y, uvy.y);
  float2 uv_ddy = float2(uvx.z, uvy.z);

  StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[g_DescIds.materialsBufferId];
  MaterialData material = materials[m.materialIndex];

  Texture2D baseColorTex = ResourceDescriptorHeap[NonUniformResourceIndex(material.baseColorId)];
  float4 baseColor = baseColorTex.SampleGrad(s1, uv, uv_ddx, uv_ddy);

  RWTexture2D<float4> gBufferBaseColor = ResourceDescriptorHeap[g_PerDispatchConstants.BaseColorId];
  gBufferBaseColor[position] = baseColor;

  // World position

  float3 worldPos = Interpolate(barycentrics.m_lambda, v0.posWS.xyz, v1.posWS.xyz, v2.posWS.xyz);

  RWTexture2D<float4> gBufferWorldPos = ResourceDescriptorHeap[g_PerDispatchConstants.WorldPositionId];
  gBufferWorldPos[position] = float4(worldPos, 1.0f);

  // World normal

  float3 worldNormal = Interpolate(barycentrics.m_lambda, v0.normalWS, v1.normalWS, v2.normalWS);
  float3 worldTangent = Interpolate(barycentrics.m_lambda, v0.tangentWS, v1.tangentWS, v2.tangentWS);
  float bitangentSign = v0.bitangentSign;
  float3 n = normalize(worldNormal);
  float3 t = normalize(worldTangent);
  float3 b = normalize(cross(n, t)) * bitangentSign;
  float3x3 tbn = float3x3(t, b, n);

  Texture2D normalMapTex = ResourceDescriptorHeap[NonUniformResourceIndex(material.normalMapId)];
  float4 normalMap = normalMapTex.SampleGrad(s1, uv, uv_ddx, uv_ddy);

  float3 tangentSpaceNormal;
  tangentSpaceNormal.xy = normalMap.xy * 2.0 - 1.0;
  tangentSpaceNormal.z = sqrt(1.0 - saturate(dot(tangentSpaceNormal.xy, tangentSpaceNormal.xy)));
  float3 finalWorldNormal = normalize(mul(tangentSpaceNormal, tbn));

  RWTexture2D<float4> worldNorm = ResourceDescriptorHeap[g_PerDispatchConstants.WorldNormalId];
  worldNorm[position] = float4(finalWorldNormal, 1.0f);
}
