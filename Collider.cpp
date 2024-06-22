#include "stdafx.h"

#include "DirectXCollision.h"
#include "Collider.h"

using namespace DirectX;

Collider::Collider() { staticFloors.clear(); }

void Collider::AppendStaticModel(Model3D* m)
{
  for (auto& sub : m->mesh->subsets) {
    unsigned int offset = sub.start;

    for (unsigned int i = 0; i < sub.count; i += 3) {
      Surface surf;
      XMVECTOR xmv1, xmv2, xmv3;

      // copy positions
      {
        int i1 = m->mesh->indices[i + offset];
        int i2 = m->mesh->indices[i + 1 + offset];
        int i3 = m->mesh->indices[i + 2 + offset];

        Vertex v1 = m->mesh->vertices[i1];
        Vertex v2 = m->mesh->vertices[i2];
        Vertex v3 = m->mesh->vertices[i3];

        xmv1 = XMVectorSet(v1.position.x, v1.position.y, v1.position.z, 1.0f);
        xmv1 = XMVector4Transform(xmv1, m->WorldMatrix());
        XMStoreFloat3(&surf.v1, xmv1);

        xmv2 = XMVectorSet(v2.position.x, v2.position.y, v2.position.z, 1.0f);
        xmv2 = XMVector4Transform(xmv2, m->WorldMatrix());
        XMStoreFloat3(&surf.v2, xmv2);

        xmv3 = XMVectorSet(v3.position.x, v3.position.y, v3.position.z, 1.0f);
        xmv3 = XMVector4Transform(xmv3, m->WorldMatrix());
        XMStoreFloat3(&surf.v3, xmv3);
      }

      // compute face normal and origin offset
      {
        XMVECTOR u, v, normal;
        u = xmv2 - xmv1;
        v = xmv3 - xmv1;
        normal = XMVector3Normalize(XMVector3Cross(u, v));
        XMStoreFloat3(&surf.normal, normal);

        surf.originOffset = XMVectorGetX(-XMVector3Dot(normal, xmv1));
      }

      static constexpr float VERTICAL_BUFFER = 0.5f;
      surf.minY = std::min({surf.v1.y, surf.v2.y, surf.v3.y}) - VERTICAL_BUFFER;
      surf.maxY = std::max({surf.v1.y, surf.v2.y, surf.v3.y}) + VERTICAL_BUFFER;

      if (surf.normal.y > 0.25)
        staticFloors.push_back(surf);
      else if (surf.normal.y < -0.25)
        staticCeils.push_back(surf);
      else
        staticWalls.push_back(surf);
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

Surface* Collider::FindFloor(DirectX::XMFLOAT3 point, float offsetY, float& prevHeight)
{
  Surface* floor = nullptr;

  prevHeight = -std::numeric_limits<float>::infinity();
  float y = point.y + offsetY;

  for (auto& surf : staticFloors) {
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

  return floor;
}

Surface* Collider::FindWall(XMVECTOR origin, XMVECTOR direction, float offsetY, float& distance)
{
  Surface* wall = nullptr;
  XMVECTOR offset = XMVectorSet(0.0f, offsetY, 0.0f, 0.0f); 
  origin += offset;

  distance = std::numeric_limits<float>::infinity();
  float hitDistance;

  for (auto& surf : staticWalls) {
    if (XMVectorGetY(origin) < surf.minY || XMVectorGetY(origin) > surf.maxY)
      continue;

    XMVECTOR p1 = XMLoadFloat3(&surf.v1);
    XMVECTOR p2 = XMLoadFloat3(&surf.v2);
    XMVECTOR p3 = XMLoadFloat3(&surf.v3);

    if (!TriangleTests::Intersects(origin, direction, p1, p2, p3, hitDistance))
      continue;

    if (hitDistance < distance) {
      distance = hitDistance;
      wall = &surf;
    }
  }

  return wall;
}
