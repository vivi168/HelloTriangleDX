#pragma once

#include <string>
#include <vector>

#define MAX_TEXTURE_NAME_LEN 64
typedef char TEXTURENAME[MAX_TEXTURE_NAME_LEN];

struct Vertex {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT4 color;
  DirectX::XMFLOAT2 uv;
};

struct Subset {
  uint32_t start, count;
  TEXTURENAME name;

  struct Texture* texture = nullptr;  // GPU data
};

struct Mesh3D {
  struct {
    uint32_t numVerts;
    uint32_t numIndices;
    uint32_t numSubsets;
  } header;

  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
  std::vector<Subset> subsets;

  std::string name;
  struct Geometry* geometry = nullptr;  // GPU data

  void Read(std::string filename);

  uint64_t VertexBufferSize() const { return sizeof(Vertex) * header.numVerts; }

  uint64_t IndexBufferSize() const
  {
    return sizeof(uint16_t) * header.numIndices;
  }
};

struct Model3D {
  std::vector<Mesh3D*> meshes;
  // TODO: skeleton
  // TODO: std::vector<animations>

  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 rotate;
  DirectX::XMFLOAT3 translate;
  bool dirty; // world position changed

  Model3D();
  DirectX::XMMATRIX WorldMatrix();

  void Scale(float s);
  void Rotate(float x, float y, float z);
  void Translate(float x, float y, float z);
  void Clean() { dirty = false;  }
};
