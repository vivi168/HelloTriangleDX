#include "Shared.h"

#define AS_GROUP_SIZE WAVE_GROUP_SIZE

struct ASPayload
{
    uint MeshletIndices[AS_GROUP_SIZE];
};

struct Vertex
{
  float4 posCS : SV_POSITION;
  uint meshletIndex : COLOR0;
};
