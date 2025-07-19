#include "Shared.h"

#define AS_GROUP_SIZE WAVE_GROUP_SIZE

struct ASPayload
{
    uint MeshletIndices[AS_GROUP_SIZE];
};

struct VertexOut
{
  float4 posCS : SV_POSITION;
  uint meshletIndex : COLOR0;
};

uint3 UnpackPrimitive(uint primitive)
{
  return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF, (primitive >> 20) & 0x3FF);
}

MeshInstanceData GetInstanceData(BuffersDescriptorIndices descIds, uint index)
{
  StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[descIds.instancesBufferId];
  return meshInstances[index];
}

MeshletData GetMeshletData(BuffersDescriptorIndices descIds, uint index)
{
  StructuredBuffer<MeshletData> meshlets = ResourceDescriptorHeap[descIds.meshletsBufferId];
  return meshlets[index];
}

uint3 GetPrimitive(BuffersDescriptorIndices descIds, uint primIndex)
{
  StructuredBuffer<uint> PrimitiveIndices = ResourceDescriptorHeap[descIds.meshletsPrimitivesBufferId];
  return UnpackPrimitive(PrimitiveIndices[primIndex]);
}

uint GetVertexIndex(BuffersDescriptorIndices descIds, uint vertexIndex)
{
  StructuredBuffer<uint> UniqueVertexIndices = ResourceDescriptorHeap[descIds.meshletVertIndicesBufferId];
  return UniqueVertexIndices[vertexIndex];
}
