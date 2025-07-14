// TODO: DRY everything!!!
cbuffer PushConstants : register(b0)
{
  uint NumInstances;
};

cbuffer FrameConstants : register(b1)
{
  float Time;
  float3 CameraWS;
  float4 FrustumPlanes[6];
  float2 ScreenSize;
};

cbuffer BuffersDescriptorIndices : register(b2)
{
  uint InstancesBufferId;
  uint DrawMeshCommandsBufferId;
};

struct MeshInstance {
  float4x4 WorldViewProj;
  float4x4 WorldMatrix;
  float4x4 NormalMatrix;
  float4 BoundingSphere;
  float scale;

  uint positionsBufferOffset;
  uint normalsBufferOffset;
  // TODO: tangents
  uint uvsBufferOffset;

  uint meshletBufferOffset;
  uint indexBufferOffset;
  uint primBufferOffset;

  uint numMeshlets;
};

struct DrawMeshCommand {
  uint instanceIndex;

  uint threadGroupCountX;
  uint threadGroupCountY;
  uint threadGroupCountZ;
};

[numthreads(64, 1, 1)]
void main(uint dtid : SV_DispatchThreadID)
{
  if (dtid >= NumInstances) return;

  AppendStructuredBuffer<DrawMeshCommand> instances = ResourceDescriptorHeap[DrawMeshCommandsBufferId];

  StructuredBuffer<MeshInstance> meshInstances = ResourceDescriptorHeap[InstancesBufferId];
  MeshInstance mi = meshInstances[dtid];

  float4 center = mul(float4(mi.BoundingSphere.xyz, 1), mi.WorldMatrix);
  float radius = mi.BoundingSphere.w * mi.scale;

  for (int i = 0; i < 6; ++i) {
    if (dot(center, FrustumPlanes[i]) < -radius) {
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
