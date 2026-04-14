#include "Shared.h"

cbuffer PushConstants : register(b0) {
  CullingBuffersDescriptorIndices g_DescIds;
  uint FrameConstantsIndex;
  uint NumInstances;
}

[NumThreads(COMPUTE_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID)
{
  if (dtid >= NumInstances) return;

  AppendStructuredBuffer<DrawMeshCommand> instances = ResourceDescriptorHeap[g_DescIds.DrawMeshCommandsBufferId];

  StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[g_DescIds.InstancesBufferId];
  MeshInstanceData mi = meshInstances[dtid];

  float4 center = mul(float4(mi.boundingSphere.xyz, 1), mi.worldMatrix);
  float radius = mi.boundingSphere.w * mi.scale;

  ConstantBuffer<FrameConstants> g_FrameConstants = ResourceDescriptorHeap[FrameConstantsIndex];

  for (int i = 0; i < 6; ++i) {
    if (dot(center, g_FrameConstants.FrustumPlanes[i]) < -radius) {
      return;
    }
  }

  DrawMeshCommand cmd;
  cmd.instanceIndex = dtid;
  cmd.threadGroupCountX = (mi.numMeshlets + 32 - 1) / 32;
  cmd.threadGroupCountY = 1;
  cmd.threadGroupCountZ = 1;

  instances.Append(cmd);
}
