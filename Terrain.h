#pragma once

#include "Mesh.h"

#define CHUNK_SIZE 16
#define CELL_SIZE 1024
#define CELL_COUNT (CHUNK_SIZE * CHUNK_SIZE)

class Chunk
{
public:
  Chunk();
  DirectX::XMMATRIX WorldMatrix();
  Mesh3D<Vertex> Mesh();

private:
  void InitHeightmap();

  int cx, cy;
  DirectX::XMFLOAT3 translate;
  std::array<std::array<float, CHUNK_SIZE + 1>, CHUNK_SIZE + 1> m_Heightmap;
  Mesh3D<Vertex> mesh;
};

class Terrain
{
public:
  Terrain();

private:
  std::vector<Chunk> chunks;
};
