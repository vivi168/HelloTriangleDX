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

struct PRIM_OUT
{
  uint PrimitiveId : SV_PrimitiveID;
  bool culled : SV_CullPrimitive;
};

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

  if (gtid < m.PrimCount)
  {
    tris[gtid] = GetPrimitive(m, mi.primBufferOffset + gtid);
    prims[gtid].PrimitiveId = gtid;
    prims[gtid].culled = false;
  }

  if (gtid < m.VertCount)
  {
    uint vertexIndex = GetVertexIndex(m, mi.indexBufferOffset + gtid);
    verts[gtid] = GetVertexAttributes(mi, gid, vertexIndex, m.materialIndex);
  }
}
