#pragma once

#include "Mesh.h"

struct Surface {
  DirectX::XMFLOAT3 v1;
  DirectX::XMFLOAT3 v2;
  DirectX::XMFLOAT3 v3;
  DirectX::XMFLOAT3 normal;

  float minY;
  float maxY;
  float originOffset;

  float HeightAt(float x, float z);
  bool WithinBound(float x, float z);
};

class Collider
{
public:
  Collider();
  void AppendStaticModel(Model3D* m);
  Surface FindFloor(DirectX::XMFLOAT3 point, float* prevHeight);

private:
  std::list<Surface> staticFloors;
  std::list<Surface> staticWalls;
  std::list<Surface> staticCeils;
  //std::list<Surface> dynamicSurfaces;
};
