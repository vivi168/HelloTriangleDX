static const uint PrimitiveBits = 7u;
static const uint PrimitiveMask = (1u << PrimitiveBits) - 1u;

struct Visibility
{
  uint primitiveIndex;
  uint meshletIndex;
};

uint PackVisibility(uint meshletIndex, uint primitiveIndex)
{
  return ((meshletIndex + 1u) << PrimitiveBits) | (primitiveIndex & PrimitiveMask);
}

Visibility UnpackVisibility(uint value)
{
  Visibility vis;

  vis.primitiveIndex = value & PrimitiveMask;
  vis.meshletIndex = (value >> PrimitiveBits) - 1u;

  return vis;
}
