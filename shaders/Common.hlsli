struct VS_INPUT
{
#ifdef SKINNED
  float3 pos : POSITION;
  float3 normal : NORMAL;
  float4 color : COLOR;
  float2 texCoord : TEXCOORD;

  float4 boneWeights : WEIGHTS;
  uint4 boneIndices : BONEINDICES;
#else
  uint vid : SV_VertexID;
#endif
};

struct VS_OUTPUT
{
  float4 pos : SV_POSITION;
  float3 normal : NORMAL;
  float4 color : COLOR;
  float2 texCoord : TEXCOORD;
};

cbuffer FrameConstants : register(b0)
{
  float time;
  float deltaTime;
};

cbuffer MaterialConstants : register(b2)
{
  uint vbIndex;
  uint vOffset;
  uint diffuseIndex;
};
