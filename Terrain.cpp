#include "stdafx.h"
#include "Terrain.h"
#include "FastNoise/FastNoise.h"

using namespace DirectX;

Chunk::Chunk()
{
  mesh = {0};
  InitHeightmap();
}

void Chunk::InitHeightmap()
{
  auto fnSimplex = FastNoise::New<FastNoise::Simplex>();
  fnSimplex->GenUniformGrid2D(m_Heightmap[0].data(), 0, 0, CHUNK_SIZE + 1,
                              CHUNK_SIZE + 1, 0.05f, 1337);

  for (int j = 0; j <= CHUNK_SIZE; j++) {
    for (int i = 0; i <= CHUNK_SIZE; i++) {
      printf("%f ", m_Heightmap[j][i]);
    }
    printf("\n");
  }
}

Mesh3D Chunk::Mesh()
{
  if (mesh.header.numVerts != 0) return mesh;

  mesh.header.numVerts = 4;
  mesh.header.numIndices = 6;
  mesh.header.numSubsets = 1;

  mesh.vertices.resize(mesh.header.numVerts);
  mesh.indices.resize(mesh.header.numIndices);
  mesh.subsets.resize(mesh.header.numSubsets);

  mesh.vertices[0] = Vertex{{-100, 0, -100}};
  mesh.vertices[1] = Vertex{{100, 0, -100}};
  mesh.vertices[2] = Vertex{{-100, 0, 100}};
  mesh.vertices[3] = Vertex{{100, 0, 100}};

  mesh.indices[0] = 0;
  mesh.indices[1] = 3;
  mesh.indices[2] = 1;
  mesh.indices[3] = 0;
  mesh.indices[4] = 2;
  mesh.indices[5] = 3;

  mesh.subsets[0] = Subset{0, 6, 0, 0, "cube.raw"};

  mesh.name = "terrain issou";

  // TODO here
  // set header
  // reserve vertices / indices vectors
  // only one subset
  // hardcode texture name for now

  /*
  mesh.header.numVerts = CELL_COUNT * 4;        // 4 vertices/cell
  mesh.header.numIndices = CELL_COUNT * 2 * 3;  // 2 tris/cell, 3 verts/tri
  mesh.header.numSubsets = 1;

  mesh.vertices.resize(mesh.header.numVerts);
  mesh.indices.resize(mesh.header.numIndices);
  mesh.subsets.resize(mesh.header.numSubsets);

  int vi = 0;
  int ii = 0;
  for (int i = 0; i < CELL_COUNT; i++) {
    int hx = i % CHUNK_SIZE;
    int hy = i / CHUNK_SIZE;

    int tl_x = hx * CELL_SIZE;
    int tl_z = hy * CELL_SIZE;

    int y = 0;

    // top left
    setVector(&chunk->vertices[vi].position, tl_x, y1, tl_z);
    setDVector(&chunk->vertices[vi].uv, 0, 0);
    // top right
    setVector(&chunk->vertices[vi + 1].position, tl_x + CELL_SIZE, y2, tl_z);
    setDVector(&chunk->vertices[vi + 1].uv, 31, 0);
    // bottom left
    setVector(&chunk->vertices[vi + 2].position, tl_x, y3, tl_z + CELL_SIZE);
    setDVector(&chunk->vertices[vi + 2].uv, 0, 31);
    // bottom right
    setVector(&chunk->vertices[vi + 3].position, tl_x + CELL_SIZE, y4,
              tl_z + CELL_SIZE);
    setDVector(&chunk->vertices[vi + 3].uv, 31, 31);

    // normals
    SVECTOR n;
    surfaceNormal(&chunk->vertices[vi].position,
                  &chunk->vertices[vi + 2].position,
                  &chunk->vertices[vi + 1].position, &n);

    // TODO: per vertex normal?
    copyVector(&chunk->vertices[vi].normal, &n);
    copyVector(&chunk->vertices[vi + 1].normal, &n);
    copyVector(&chunk->vertices[vi + 2].normal, &n);
    copyVector(&chunk->vertices[vi + 3].normal, &n);

    // indices
    chunk->indices[ii] = vi;
    chunk->indices[ii + 1] = vi + 2;
    chunk->indices[ii + 2] = vi + 1;

    chunk->indices[ii + 3] = vi + 2;
    chunk->indices[ii + 4] = vi + 3;
    chunk->indices[ii + 5] = vi + 1;

    vi += 4;
    ii += 6;
  }
  */
  // TODO
  return mesh;
}

XMMATRIX Chunk::WorldMatrix()
{
  XMVECTOR transVector = XMLoadFloat3(&translate);

  return XMMatrixTranslationFromVector(transVector);
}
