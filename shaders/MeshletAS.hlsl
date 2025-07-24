#include "MeshletCommon.hlsli"

cbuffer PerDrawConstants : register(b0)
{
  uint InstanceIndex;
};

ConstantBuffer<FrameConstants> g_FrameConstants : register(b1);

ConstantBuffer<BuffersDescriptorIndices> g_DescIds : register(b2);

groupshared ASPayload s_Payload;

bool IsConeDegenerate(MeshletData m) { return (m.normalCone >> 24) == 0xff; }

float4 UnpackCone(uint packed)
{
  float4 v;
  v.x = float((packed >> 0) & 0xff);
  v.y = float((packed >> 8) & 0xff);
  v.z = float((packed >> 16) & 0xff);
  v.w = float((packed >> 24) & 0xff);

  v = v / 255.0;
  v.xyz = v.xyz * 2.0 - 1.0;

  return v;
}

bool IsVisible(MeshletData m, float4x4 world, float scale, float3 viewPos)
{
  // Do a cull test of the bounding sphere against the view frustum planes
  float4 center = mul(float4(m.boundingSphere.xyz, 1), world);
  float radius = m.boundingSphere.w * scale;

  for (int i = 0; i < 6; ++i) {
    if (dot(center, g_FrameConstants.FrustumPlanes[i]) < -radius) {
      return false;
    }
  }

  // Cone is degenerate - spread is wider than a hemisphere
  if (IsConeDegenerate(m)) {
    return true;
  }

  float4 normalCone = UnpackCone(m.normalCone);
  float3 axis = normalize(mul(float4(normalCone.xyz, 0), world)).xyz;

  // Offset the normal cone axis from the meshlet center-point - make sure to account for world scaling
  float3 apex = center.xyz - axis * m.apexOffset * scale;
  float3 view = normalize(viewPos - apex);

  // The normal cone w-component stores -cos(angle + 90 deg)
  // This is the min dot product along the inverted axis from which all the meshlet's triangles are backface
  if (dot(view, -axis) > normalCone.w) {
    return false;
  }

  return true;
}

[NumThreads(AS_GROUP_SIZE, 1, 1)]
void main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
  bool visible = false;

  StructuredBuffer<MeshInstanceData> meshInstances = ResourceDescriptorHeap[g_DescIds.instancesBufferId];
  MeshInstanceData mi = meshInstances[InstanceIndex];

  StructuredBuffer<MeshletData> meshlets = ResourceDescriptorHeap[g_DescIds.meshletsBufferId];
  MeshletData m = meshlets[mi.firstMeshlet + dtid];

  StructuredBuffer<MaterialData> materials = ResourceDescriptorHeap[g_DescIds.materialsBufferId];
  MaterialData material = materials[m.materialIndex];

  if (dtid < mi.numMeshlets) {
    visible = IsVisible(m, mi.worldMatrix, mi.scale, g_FrameConstants.CameraWS);
  }

  if (visible) {
    uint index = WavePrefixCountBits(visible);
    s_Payload.MeshletIndices[index] = dtid;
    s_Payload.TextureIndices[index] = material.baseColorId;
  }

  uint visibleCount = WaveActiveCountBits(visible);
  DispatchMesh(visibleCount, 1, 1, s_Payload);
}
