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

struct Texture
{
    struct Header {
        uint16_t width;
        uint16_t height;
    } header;
    std::vector<uint8_t> pixels;

    void Read(std::string filename);

    DXGI_FORMAT Format() const
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    uint32_t BytesPerPixel() const
    {
        return 4;
    }

    uint32_t Width() const
    {
        return header.width;
    }

    uint32_t Height() const
    {
        return header.height;
    }

    uint64_t BytesPerRow() const
    {
        return Width() * BytesPerPixel();
    }

    uint64_t ImageSize() const
    {
        return Height() * BytesPerRow();
    }
};

struct Subset
{
    uint32_t start, count;
    TEXTURENAME name;

    Texture* texture = nullptr;
};

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

    // TODO: automatically read textures as well
    void Read(std::string filename);

    uint64_t VertexBufferSize() const
    {
        return sizeof(Vertex) * header.numVerts;
    }

    uint64_t IndexBufferSize() const
    {
        return sizeof(uint16_t) * header.numIndices;
    }
};
