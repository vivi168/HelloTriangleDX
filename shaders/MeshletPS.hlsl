#include "MeshletCommon.hlsli"

uint main(MS_OUTPUT input, uint primitiveId : SV_PrimitiveID) : SV_TARGET
{
  return ((input.meshletIndex + 1) << 7) | (primitiveId & 0x7f);
}
