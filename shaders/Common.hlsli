struct VS_INPUT
{
  float3 pos : POSITION;
  float3 normal : NORMAL;
  float4 color : COLOR;
  float2 texCoord : TEXCOORD;
#ifdef SKINNED
  float4 boneWeights : WEIGHTS;
  uint4 boneIndices : BONEINDICES;
#endif
};

struct VS_INPUT_TERRAIN
{
  float3 pos : POSITION;
  float3 normal : NORMAL;
  float2 texCoord : TEXCOORD;
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
  uint diffuseIndex;
};
