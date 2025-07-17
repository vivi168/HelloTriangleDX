#include "Shared.h"
#include "MeshletCommon.hlsli"

cbuffer PerDrawConstants : register(b0)
{
  uint InstanceIndex;
};

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

uint3 UnpackPrimitive(uint primitive)
{
  return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

uint3 GetPrimitive(MeshletData m, uint primIndex)
{
  StructuredBuffer<uint> PrimitiveIndices = ResourceDescriptorHeap[g_DescIds.meshletsPrimitivesBufferId];

  return UnpackPrimitive(PrimitiveIndices[m.firstPrim + primIndex]);
}

uint GetVertexIndex(MeshletData m, uint localIndex)
{
  localIndex = m.firstVert + localIndex;
  StructuredBuffer<uint> UniqueVertexIndices = ResourceDescriptorHeap[g_DescIds.meshletVertIndicesBufferId];

  return UniqueVertexIndices[localIndex];
}

Vertex GetVertexAttributes(MeshInstanceData mi, uint meshletIndex, uint vertexIndex)
{
  StructuredBuffer<float3> positions = ResourceDescriptorHeap[g_DescIds.vertexPositionsBufferId];
  float3 position = positions[mi.firstPosition + vertexIndex];

  Vertex vout;
  vout.posCS = mul(float4(position, 1.0f), mi.worldViewProj);
  vout.meshletIndex = meshletIndex;

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
    out vertices Vertex verts[MESHLET_MAX_VERT]
)
{
  StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[g_DescIds.instancesBufferId];
  MeshInstanceData mi = meshInstances[InstanceIndex];

  uint meshletIndex = payload.MeshletIndices[gid];
  if (meshletIndex >= mi.numMeshlets) return;

  StructuredBuffer<MeshletData> meshlets = ResourceDescriptorHeap[g_DescIds.meshletsBufferId];
  MeshletData m = meshlets[mi.firstMeshlet + meshletIndex];

  SetMeshOutputCounts(m.numVerts, m.numPrims);

  if (gtid < m.numVerts)
  {
    uint vertexIndex = GetVertexIndex(m, mi.firstVertIndex + gtid);
    Vertex v = GetVertexAttributes(mi, mi.firstMeshlet + meshletIndex, vertexIndex);
    verts[gtid] = v;
    s_PositionsCS[gtid] = float3(ClipToScreen(v.posCS.xy / v.posCS.w, g_FrameConstants.ScreenSize), v.posCS.w);
  }

  GroupMemoryBarrierWithGroupSync();

  if (gtid < m.numPrims)
  {
    uint3 tri = GetPrimitive(m, mi.firstPrimitive + gtid);
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
