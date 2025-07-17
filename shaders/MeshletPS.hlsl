#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

uint main(Vertex vin, uint primitiveIndex : SV_PrimitiveID) : SV_Target
{
  return PackVisibility(vin.meshletIndex, primitiveIndex);
}
