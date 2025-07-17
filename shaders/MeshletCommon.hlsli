#define WAVE_GROUP_SIZE 32

struct ASPayload
{
    uint MeshletIndices[WAVE_GROUP_SIZE];
};

struct Vertex
{
  float4 posCS : SV_POSITION;
  uint meshletIndex : COLOR0;
};
