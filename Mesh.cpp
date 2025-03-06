#include "stdafx.h"
#include "Mesh.h"

using namespace DirectX;

void Mesh3D::Read(std::string filename)
{
  FILE* fp;
  fopen_s(&fp, filename.c_str(), "rb");
  assert(fp);

  name = filename;

  fread(&header, sizeof(header), 1, fp);

  vertices.resize(header.numVerts);
  indices.resize(header.numIndices);
  subsets.resize(header.numSubsets);

  fread(vertices.data(), sizeof(Vertex), header.numVerts, fp);
  fread(indices.data(), sizeof(uint16_t), header.numIndices, fp);
  fread(subsets.data(), sizeof(Subset), header.numSubsets, fp);

  fclose(fp);
}

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
