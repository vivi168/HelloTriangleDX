#include "stdafx.h"
#include "Mesh.h"

using namespace DirectX;

Model3D::Model3D()
    : scale(1.f, 1.f, 1.f),
      rotate(0.f, 0.f, 0.f),
      translate(0.f, 0.f, 0.f),
      dirty(false)
{
}

DirectX::XMMATRIX Model3D::WorldMatrix()
{
  XMVECTOR scaleVector = XMLoadFloat3(&scale);
  XMVECTOR transVector = XMLoadFloat3(&translate);
  XMVECTOR rotVector = XMLoadFloat3(&rotate);
  XMVECTOR q = XMQuaternionRotationRollPitchYawFromVector(rotVector);

  XMMATRIX scaleMatrix = XMMatrixScalingFromVector(scaleVector);
  XMMATRIX transMatrix = XMMatrixTranslationFromVector(transVector);
  XMMATRIX rotMatrix = XMMatrixRotationQuaternion(q);

  return scaleMatrix * rotMatrix * transMatrix;
}

void Model3D::Scale(float s)
{
  scale = {s, s, s};
  dirty = true;
}

void Model3D::Rotate(float x, float y, float z)
{
  rotate = {x, y, z};
  dirty = true;
}

void Model3D::Translate(float x, float y, float z)
{
  translate = {x, y, z};
  dirty = true;
}
