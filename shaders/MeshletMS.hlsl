#include "MeshletCommon.hlsli"

cbuffer PerDrawConstants : register(b0)
{
  uint InstanceIndex;
};

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

VertexOut GetVertexAttributes(MeshInstanceData mi, uint meshletIndex, uint vertexIndex, uint textureIndex)
{
  StructuredBuffer<float3> positions = ResourceDescriptorHeap[g_DescIds.vertexPositionsBufferId];
  float3 position = positions[mi.firstPosition + vertexIndex];

  StructuredBuffer<float2> uvs = ResourceDescriptorHeap[g_DescIds.vertexUVsBufferId];
  float2 uv = uvs[mi.firstUV + vertexIndex];

  VertexOut vout;
  vout.posCS = mul(float4(position, 1.0f), mi.worldViewProj);
  vout.meshletIndex = meshletIndex;
  vout.textureIndex = textureIndex;
  vout.uv = uv;

  return vout;
}

float2 ClipToUv(float2 clip) { return clip * 0.5 + 0.5; }

float2 ClipToScreen(float2 clip, float2 screen) { return ClipToUv(clip) * screen; }

struct PRIM_OUT
{
  uint PrimitiveId : SV_PrimitiveID;
  bool culled : SV_CullPrimitive;
};

groupshared float3 s_PositionsCS[MESHLET_MAX_VERT];

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    in payload ASPayload payload,
    out indices uint3 tris[MESHLET_MAX_PRIM],
    out primitives PRIM_OUT prims[MESHLET_MAX_PRIM],
    out vertices VertexOut verts[MESHLET_MAX_VERT]
)
{
  StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[g_DescIds.instancesBufferId];
  MeshInstanceData mi = meshInstances[InstanceIndex];

  uint meshletIndex = payload.MeshletIndices[gid];
  uint textureIndex = payload.TextureIndices[gid];

  if (meshletIndex >= mi.numMeshlets) return;

  StructuredBuffer<MeshletData> meshlets = ResourceDescriptorHeap[g_DescIds.meshletsBufferId];
  MeshletData m = meshlets[mi.firstMeshlet + meshletIndex];

  SetMeshOutputCounts(m.numVerts, m.numPrims);

  if (gtid < m.numVerts)
  {
    uint vertexIndex = GetVertexIndex(g_DescIds, mi.firstVertIndex + m.firstVert + gtid);
    VertexOut v = GetVertexAttributes(mi, mi.firstMeshlet + meshletIndex, vertexIndex, textureIndex);
    verts[gtid] = v;
    s_PositionsCS[gtid] = float3(ClipToScreen(v.posCS.xy / v.posCS.w, g_FrameConstants.ScreenSize), v.posCS.w);
  }

  GroupMemoryBarrierWithGroupSync();

  if (gtid < m.numPrims)
  {
    uint3 tri = GetPrimitive(g_DescIds, mi.firstPrimitive + m.firstPrim + gtid);
    tris[gtid] = tri;

    bool culled = false;

    // https://github.com/zeux/niagara
    // Backface culling + zero area culling
    float2 pa = s_PositionsCS[tri.x].xy;
    float2 pb = s_PositionsCS[tri.y].xy;
    float2 pc = s_PositionsCS[tri.z].xy;

    float2 eb = pb - pa;
    float2 ec = pc - pa;

    culled = culled || (eb.x * ec.y <= eb.y * ec.x);

    float2 bmin = min(pa, min(pb, pc));
    float2 bmax = max(pa, max(pb, pc));
    float sbprec = 1.0 / 256.0;  // note: this can be set to 1/2^subpixelPrecisionBits

    // Note: this is slightly imprecise (doesn't fully match hw behavior and is both too loose and too strict)
    culled = culled || (round(bmin.x - sbprec) == round(bmax.x) || round(bmin.y) == round(bmax.y + sbprec));

    // Above computations valid only if all vertices are in front of perspective plane
    culled = culled && (s_PositionsCS[tri.x].z > 0 && s_PositionsCS[tri.y].z > 0 && s_PositionsCS[tri.z].z > 0);

    prims[gtid].PrimitiveId = gtid;
    prims[gtid].culled = culled;
  }
}
