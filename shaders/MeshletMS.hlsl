cbuffer FrameConstants : register(b0)
{
  float time;
  float deltaTime;

  uint vertexPositionsBufferId;
  uint vertexNormalsBufferId;
  // TODO: tangents
  uint vertexUVsBufferId;
  // TODO: blend

  uint meshletsBufferId;
  uint visibleMeshletsBufferId;
  uint meshletUniqueIndicesBufferId;
  uint meshletsPrimitivesBufferId;
  uint meshletMaterialsBufferId;
  // TODO: materials
  uint instancesBufferId;
};

cbuffer ObjectCb : register(b1)
{
  float4x4 WorldViewProj;
  float4x4 WorldMatrix;
  float4x4 NormalMatrix;
};

cbuffer MeshletConstants : register(b3)
{
  uint instanceBufferId;
  uint vertexBufferId;
  uint meshletBufferId;
  uint indexBufferId;
  uint primBufferId;
  uint materialBufferId;
};

struct MeshInstance {
  float4x4 WorldViewProj;
  float4x4 WorldMatrix;
  float4x4 NormalMatrix;

  uint positionsBufferId;
  uint normalsBufferId;
  // TODO: tangents
  uint uvsBufferId;

  uint meshletBufferId;
  uint indexBufferId;
  uint primBufferId;
  uint materialBufferId;
  uint pad;
};

struct Meshlet
{
  uint VertCount;
  uint VertOffset;
  uint PrimCount;
  uint PrimOffset;
};

struct Vertex
{
  float3 pos;
  float3 normal;
  float4 color;
  float2 texCoord;
};

struct MS_OUTPUT
{
  float4 pos : SV_POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  uint meshletIndex : COLOR0;
};

uint3 UnpackPrimitive(uint primitive)
{
  return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

uint3 GetPrimitive(Meshlet m, uint gtid)
{
  StructuredBuffer<uint> PrimitiveIndices = ResourceDescriptorHeap[primBufferId];
  
  return UnpackPrimitive(PrimitiveIndices[m.PrimOffset + gtid]);
}

uint GetVertexIndex(Meshlet m, uint localIndex)
{
  localIndex = m.VertOffset + localIndex;
  StructuredBuffer<uint> UniqueVertexIndices = ResourceDescriptorHeap[indexBufferId];
  
  return UniqueVertexIndices[localIndex];
}

MS_OUTPUT GetVertexAttributes(uint meshletIndex, uint vertexIndex)
{
  StructuredBuffer<Vertex> Vertices = ResourceDescriptorHeap[vertexBufferId];
  Vertex v = Vertices[vertexIndex];

  MS_OUTPUT vout;
  vout.pos = mul(float4(v.pos, 1.0f), WorldViewProj);
  float3 norm = mul(float4(v.normal, 1.0f), NormalMatrix).xyz;
  vout.normal = normalize(norm);
  vout.meshletIndex = meshletIndex;
  vout.texCoord = v.texCoord;

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
    uint gid : SV_GroupID,
    out indices uint3 tris[124],
    out vertices MS_OUTPUT verts[64],
    out primitives PRIM_OUT prims[124]
)
{
  StructuredBuffer<Meshlet> meshlets = ResourceDescriptorHeap[meshletBufferId];
  Meshlet m = meshlets[gid];
  
  SetMeshOutputCounts(m.VertCount, m.PrimCount);

  if (gtid < m.PrimCount)
  {
    tris[gtid] = GetPrimitive(m, gtid);
    prims[gtid].PrimitiveId = gtid;
  }

  if (gtid < m.VertCount)
  {
    uint vertexIndex = GetVertexIndex(m, gtid);
    verts[gtid] = GetVertexAttributes(gid, vertexIndex);
  }
}
