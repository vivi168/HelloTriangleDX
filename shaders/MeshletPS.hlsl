cbuffer MeshletConstants : register(b3)
{
  uint vertexBufferId;
  uint meshletBufferId;
  uint indexBufferId;
  uint primBufferId;
  uint materialBufferId;
};

struct MS_OUTPUT
{
  float4 pos : SV_POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
  uint meshletIndex : COLOR0;
};

SamplerState s0 : register(s0);

float4 main(MS_OUTPUT input) : SV_TARGET
{
  uint meshletIndex = input.meshletIndex;

  StructuredBuffer<uint> MaterialIndices = ResourceDescriptorHeap[materialBufferId];
  uint materialId = MaterialIndices[meshletIndex];

  Texture2D tex = ResourceDescriptorHeap[NonUniformResourceIndex(materialId)];
  float4 diffuseColor = tex.Sample(s0, input.texCoord);

  if (diffuseColor.a == 0)
    discard;
  
  float4 meshletColor = float4(
            float(meshletIndex & 1),
            float(meshletIndex & 3) / 4,
            float(meshletIndex & 7) / 8,
            1.0f);

  return diffuseColor;
}
