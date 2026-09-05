#include "Shared.h"

ConstantBuffer<InstanceCullingPassArgs> g_Args : register(b0);

[NumThreads(COMPUTE_GROUP_SIZE, 1, 1)]
void main(uint dtid : SV_DispatchThreadID)
{
  if (dtid >= g_Args.NumInstances) return;

  AppendStructuredBuffer<DrawMeshCommand> instances = ResourceDescriptorHeap[g_Args.buffers.DrawMeshCommandsBufferId];

  StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[g_Args.buffers.InstancesBufferId];
  MeshInstanceData mi = meshInstances[dtid];

  float4 center = mul(float4(mi.boundingSphere.xyz, 1), mi.worldMatrix);
  float radius = mi.boundingSphere.w * mi.scale;

  ConstantBuffer<FrameConstants> g_FrameConstants = ResourceDescriptorHeap[g_Args.FrameConstantsIndex];

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
