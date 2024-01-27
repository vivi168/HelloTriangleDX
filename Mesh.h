#pragma once

#include <string>
#include <vector>

#define MAX_TEXTURE_NAME_LEN 64
typedef char TEXTURENAME[MAX_TEXTURE_NAME_LEN];

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT2 uv;
};

struct Texture; // GPU data

struct Subset
{
    uint32_t start, count;
    TEXTURENAME name;

    Texture* texture = nullptr;
};

struct Geometry; // GPU data

struct Mesh3D
{
    struct Header {
        uint32_t numVerts;
        uint32_t numIndices;
        uint32_t numSubsets;
    } header;
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<Subset> subsets;

    std::string name;
    Geometry* geometry = nullptr;

    // TODO: automatically read textures as well
    void Read(std::string filename);
    void CreateTextures();

    uint64_t VertexBufferSize() const
    {
        return sizeof(Vertex) * header.numVerts;
    }

    uint64_t IndexBufferSize() const
    {
        return sizeof(uint16_t) * header.numIndices;
    }
};

struct Model3D
{
    Mesh3D* mesh = nullptr;

    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT3 rotate;
    DirectX::XMFLOAT3 translate;

    Model3D();
    DirectX::XMMATRIX WorldMatrix();

    void Scale(float s)
    {
        scale = { s, s, s };
    }

    void Rotate(float x, float y, float z)
    {
        rotate = { x, y, z };
    }

    void Translate(float x, float y, float z)
    {
        translate = { x, y, z };
    }
};
