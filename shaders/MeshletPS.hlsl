#include "MeshletCommon.hlsli"
#include "VisibilityBufferCommon.hlsli"

uint main(VertexOut v, uint primitiveIndex : SV_PrimitiveID) : SV_Target
{
  return PackVisibility(v.meshletIndex, primitiveIndex);
}
