#include "stdafx.h"

#include "DirectXCollision.h"
#include "Collider.h"

using namespace DirectX;

Collider::Collider() {}

void Collider::AppendModel(Model3D* m)
{
  ColliderNode node;

  node.model = m;
  node.CreateSurfacesFromModel();
  m_ColliderNodes.push_back(node);
}

void Collider::RefreshDynamicModels()
{
  for (auto& node : m_ColliderNodes) {
    if (!node.model->dirty) continue;

    node.CreateSurfacesFromModel();
  }
}

void Collider::ColliderNode::CreateSurfacesFromModel()
{
  Clear();
  model->Clean();

  for (auto& sub : model->mesh->subsets) {
    unsigned int offset = sub.start;

    for (unsigned int i = 0; i < sub.count; i += 3) {
      Surface surf;
      XMVECTOR xmv1, xmv2, xmv3;

      // copy positions
      {
        int i1 = model->mesh->indices[i + offset];
        int i2 = model->mesh->indices[i + 1 + offset];
        int i3 = model->mesh->indices[i + 2 + offset];

        Vertex v1 = model->mesh->vertices[i1];
        Vertex v2 = model->mesh->vertices[i2];
        Vertex v3 = model->mesh->vertices[i3];

        xmv1 = XMVectorSet(v1.position.x, v1.position.y, v1.position.z, 1.0f);
        xmv1 = XMVector4Transform(xmv1, model->WorldMatrix());
        XMStoreFloat3(&surf.v1, xmv1);

        xmv2 = XMVectorSet(v2.position.x, v2.position.y, v2.position.z, 1.0f);
        xmv2 = XMVector4Transform(xmv2, model->WorldMatrix());
        XMStoreFloat3(&surf.v2, xmv2);

        xmv3 = XMVectorSet(v3.position.x, v3.position.y, v3.position.z, 1.0f);
        xmv3 = XMVector4Transform(xmv3, model->WorldMatrix());
        XMStoreFloat3(&surf.v3, xmv3);
      }

      // compute face normal and origin offset
      {
        XMVECTOR u, v, normal;
        u = xmv2 - xmv1;
        v = xmv3 - xmv1;
        normal = XMVector3Normalize(XMVector3Cross(u, v));
        XMStoreFloat3(&surf.normal, normal);

        XMStoreFloat(&surf.originOffset, -XMVector3Dot(normal, xmv1));
      }

      surf.minY = std::min({surf.v1.y, surf.v2.y, surf.v3.y});
      surf.maxY = std::max({surf.v1.y, surf.v2.y, surf.v3.y});

      if (surf.normal.y > 0.25)
        floors.push_back(surf);
      else if (surf.normal.y < -0.25)
        ceilings.push_back(surf);
      else
        walls.push_back(surf);
    }
  }
}

float Surface::HeightAt(float x, float z)
{
  return -(x * normal.x + z * normal.z + originOffset) / normal.y;
}

bool Surface::WithinBound(float x, float z)
{
  if ((v1.z - z) * (v2.x - v1.x) - (v1.x - x) * (v2.z - v1.z) < 0) return false;
  if ((v2.z - z) * (v3.x - v2.x) - (v2.x - x) * (v3.z - v2.z) < 0) return false;
  if ((v3.z - z) * (v1.x - v3.x) - (v3.x - x) * (v1.z - v3.z) < 0) return false;

  return true;
}

Surface* Collider::FindFloor(DirectX::XMFLOAT3 point, float offsetY,
                             float& prevHeight)
{
  Surface* floor = nullptr;
  prevHeight = -std::numeric_limits<float>::infinity();
  float y = point.y + offsetY;

  for (auto& node : m_ColliderNodes) {
    for (auto& surf : node.floors) {
      // skip floors above point
      if (y < surf.minY) continue;

      if (!surf.WithinBound(point.x, point.z)) continue;

      float height = surf.HeightAt(point.x, point.z);

      // skip floor lower than previous highest floor
      if (height <= prevHeight) continue;

      // skip if not inside floor hitbox
      if (y < height) continue;

      prevHeight = height;
      floor = &surf;
    }
  }

  return floor;
}

Surface* Collider::FindWall(XMVECTOR origin, XMVECTOR direction, float offsetY,
                            float& distance)
{
  Surface* wall = nullptr;
  XMVECTOR offset = XMVectorSet(0.0f, offsetY, 0.0f, 0.0f);
  origin += offset;

  distance = std::numeric_limits<float>::infinity();
  float hitDistance;

  for (auto& node : m_ColliderNodes) {
    for (auto& surf : node.walls) {
      if (XMVectorGetY(origin) < surf.minY || XMVectorGetY(origin) > surf.maxY)
        continue;

      XMVECTOR p1 = XMLoadFloat3(&surf.v1);
      XMVECTOR p2 = XMLoadFloat3(&surf.v2);
      XMVECTOR p3 = XMLoadFloat3(&surf.v3);

      if (!TriangleTests::Intersects(origin, direction, p1, p2, p3,
                                     hitDistance))
        continue;

      if (hitDistance < distance) {
        distance = hitDistance;
        wall = &surf;
      }
    }
  }

  return wall;
}
