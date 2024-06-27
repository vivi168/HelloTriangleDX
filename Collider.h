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
  void AppendModel(Model3D* m);
  Surface* FindFloor(DirectX::XMFLOAT3 point, float offsetY, float& prevHeight);
  Surface* FindWall(DirectX::XMVECTOR point, DirectX::XMVECTOR direction,
                    float offsetY, float& distance);

  void RefreshDynamicModels();

private:
  struct ColliderNode {
    std::list<Surface> floors;
    std::list<Surface> walls;
    std::list<Surface> ceilings;

    Model3D* model;

    void CreateSurfacesFromModel();
    void Clear()
    {
      floors.clear();
      walls.clear();
      ceilings.clear();
    }
  };

  //void CreateSurfacesFromModel(ColliderNode* node, Model3D* m);
  void FindFloorInList(std::list<Surface>& list, DirectX::XMFLOAT3 point, float offsetY, float& prevHeight);
  Surface* FindWallInList(std::list<Surface>& list, DirectX::XMVECTOR point, DirectX::XMVECTOR direction,
                    float offsetY, float& distance);

  std::list<ColliderNode> m_ColliderNodes;
};
