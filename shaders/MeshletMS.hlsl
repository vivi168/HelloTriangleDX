#include "MeshletCommon.hlsli"

struct Vertex
{
  float3 pos;
  float3 normal;
  float4 color;
  float2 texCoord;
};

uint3 UnpackPrimitive(uint primitive)
{
  return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

uint3 GetPrimitive(Meshlet m, uint primOffset)
{
  StructuredBuffer<uint> PrimitiveIndices = ResourceDescriptorHeap[meshletsPrimitivesBufferId];

  return UnpackPrimitive(PrimitiveIndices[m.PrimOffset + primOffset]);
}

uint GetVertexIndex(Meshlet m, uint localIndex)
{
  localIndex = m.VertOffset + localIndex;
  StructuredBuffer<uint> UniqueVertexIndices = ResourceDescriptorHeap[meshletUniqueIndicesBufferId];

  return UniqueVertexIndices[localIndex];
}

MS_OUTPUT GetVertexAttributes(MeshInstance mi, uint meshletIndex, uint vertexIndex, uint materialIndex)
{
  StructuredBuffer<float3> positions = ResourceDescriptorHeap[vertexPositionsBufferId];
  StructuredBuffer<float3> normals = ResourceDescriptorHeap[vertexNormalsBufferId];
  StructuredBuffer<float3> uvs = ResourceDescriptorHeap[vertexUVsBufferId];
  float3 position = positions[mi.positionsBufferOffset + vertexIndex];
  float3 normal = normals[mi.normalsBufferOffset + vertexIndex];
  float2 uv = uvs[mi.uvsBufferOffset + vertexIndex];

  MS_OUTPUT vout;
  vout.posCS = mul(float4(position, 1.0f), mi.WorldViewProj);
  vout.posWS = mul(float4(position, 1.0f), mi.WorldMatrix);
  float3 norm = mul(float4(normal, 1.0f), mi.NormalMatrix).xyz;
  vout.normal = normalize(norm);
  vout.meshletIndex = mi.meshletBufferOffset + meshletIndex;
  vout.materialIndex = materialIndex;
  vout.texCoord = uv;

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
    out vertices MS_OUTPUT verts[MESHLET_MAX_VERT]
)
{
  StructuredBuffer<MeshInstance> meshInstances = ResourceDescriptorHeap[instancesBufferId];
  MeshInstance mi = meshInstances[instanceBufferOffset];

  uint meshletIndex = payload.MeshletIndices[gid];
  if (meshletIndex >= numMeshlets) return;

  StructuredBuffer<Meshlet> meshlets = ResourceDescriptorHeap[meshletsBufferId];
  Meshlet m = meshlets[mi.meshletBufferOffset + meshletIndex];

  SetMeshOutputCounts(m.VertCount, m.PrimCount);

  if (gtid < m.VertCount)
  {
    uint vertexIndex = GetVertexIndex(m, mi.indexBufferOffset + gtid);
    MS_OUTPUT v = GetVertexAttributes(mi, gid, vertexIndex, m.materialIndex);
    verts[gtid] = v;
    s_PositionsCS[gtid] = float3(ClipToScreen(v.posCS.xy / v.posCS.w, ScreenSize), v.posCS.w);
  }

  GroupMemoryBarrierWithGroupSync();

  if (gtid < m.PrimCount)
  {
    uint3 tri = GetPrimitive(m, mi.primBufferOffset + gtid);
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
