#include "MeshletCommon.hlsli"

struct Meshlet
{
  uint VertCount;
  uint VertOffset;
  uint PrimCount;
  uint PrimOffset;

  uint subsetIndex;
  float4 boundingSphere;
  uint normalCone;
  float apexOffset;
  uint pad;
};

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

uint3 GetPrimitive(Meshlet m, uint gtid)
{
  StructuredBuffer<uint> PrimitiveIndices = ResourceDescriptorHeap[meshletsPrimitivesBufferId];

  return UnpackPrimitive(PrimitiveIndices[m.PrimOffset + gtid]);
}

uint GetVertexIndex(Meshlet m, uint localIndex)
{
  localIndex = m.VertOffset + localIndex;
  StructuredBuffer<uint> UniqueVertexIndices = ResourceDescriptorHeap[meshletUniqueIndicesBufferId];

  return UniqueVertexIndices[localIndex];
}

MS_OUTPUT GetVertexAttributes(MeshInstance mi, uint meshletIndex, uint vertexIndex)
{
  StructuredBuffer<float3> positions = ResourceDescriptorHeap[vertexPositionsBufferId];
  StructuredBuffer<float3> normals = ResourceDescriptorHeap[vertexNormalsBufferId];
  StructuredBuffer<float3> uvs = ResourceDescriptorHeap[vertexUVsBufferId];
  float3 position = positions[mi.positionsBufferOffset + vertexIndex];
  float3 normal = normals[mi.normalsBufferOffset + vertexIndex];
  float2 uv = uvs[mi.uvsBufferOffset + vertexIndex];

  MS_OUTPUT vout;
  vout.pos = mul(float4(position, 1.0f), mi.WorldViewProj);
  float3 norm = mul(float4(normal, 1.0f), mi.NormalMatrix).xyz;
  vout.normal = normalize(norm);
  vout.meshletIndex = mi.meshletBufferOffset + meshletIndex;
  vout.texCoord = uv;

  return vout;
}

struct PRIM_OUT
{
  uint PrimitiveId : SV_PrimitiveID;
};

[NumThreads(128, 1, 1)]
[OutputTopology("triangle")]
void main(
    uint gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID,
    out indices uint3 tris[124],
    out vertices MS_OUTPUT verts[64],
    out primitives PRIM_OUT prims[124]
)
{
  StructuredBuffer<MeshInstance> meshInstances = ResourceDescriptorHeap[instancesBufferId];
  MeshInstance mi = meshInstances[instanceBufferOffset + gid.y];

  StructuredBuffer<Meshlet> meshlets = ResourceDescriptorHeap[meshletsBufferId];
  Meshlet m = meshlets[mi.meshletBufferOffset + gid.x];

  SetMeshOutputCounts(m.VertCount, m.PrimCount);

  if (gtid < m.PrimCount)
  {
    tris[gtid] = GetPrimitive(m, mi.primBufferOffset + gtid);
    prims[gtid].PrimitiveId = gtid;
  }

  if (gtid < m.VertCount)
  {
    uint vertexIndex = GetVertexIndex(m, mi.indexBufferOffset + gtid);
    verts[gtid] = GetVertexAttributes(mi, gid.x, vertexIndex);
  }
}
