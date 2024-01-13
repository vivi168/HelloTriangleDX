#pragma once

#include <string>
#include <vector>

typedef char STRING20[20];

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 color;
    DirectX::XMFLOAT2 uv;
};

struct Texture;

struct Subset
{
    unsigned int start, count;
    Texture* texture;
    STRING20 name;
};

struct Mesh3D
{
    struct Header {
        int numVerts;
        int numIndices;
        int numSubsets;
    } header;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Subset> subsets;

    void read(std::string filename);
};