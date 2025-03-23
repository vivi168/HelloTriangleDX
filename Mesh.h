#pragma once

#include <string>
#include <vector>

#define MAX_TEXTURE_NAME_LEN 64
typedef char TEXTURENAME[MAX_TEXTURE_NAME_LEN];

#define MAX_WEIGHTS 4

struct Vertex {
  DirectX::XMFLOAT3 position;
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT4 color;
  DirectX::XMFLOAT2 uv;
};

struct SkinnedVertex : Vertex {
  UCHAR weights[MAX_WEIGHTS];
  UCHAR joints[MAX_WEIGHTS];
};

struct Skin {
  struct {
    UINT rootBone;
    UINT numBones;
    UINT numJoints;
  } header;

  // child -> parent
  std::unordered_map<SHORT, SHORT> boneHierarchy;
  // joint -> matrix
  std::unordered_map<SHORT, DirectX::XMFLOAT4X4> inverseBindMatrices;

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    fread(&header, sizeof(header), 1, fp);

    // bone hierarchy
    std::vector<SHORT> childBones;
    std::vector<SHORT> parentBones;
    childBones.resize(header.numBones);
    parentBones.resize(header.numBones);

    fread(childBones.data(), sizeof(SHORT), header.numBones, fp);
    fread(parentBones.data(), sizeof(SHORT), header.numBones, fp);

    for (int i = 0; i < header.numBones; i++) {
      boneHierarchy[childBones[i]] = parentBones[i];
    }

    // joints + inverse bind matrices
    std::vector<SHORT> jointIndices;
    std::vector<DirectX::XMFLOAT4X4> matrices;
    jointIndices.resize(header.numJoints);
    matrices.resize(header.numJoints);

    fread(jointIndices.data(), sizeof(SHORT), header.numJoints, fp);
    fread(matrices.data(), sizeof(DirectX::XMFLOAT4X4), header.numJoints, fp);

    for (int i = 0; i < header.numJoints; i++) {
      inverseBindMatrices[jointIndices[i]] = matrices[i];
    }
  }
};

struct Keyframe {
  float time;
  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 translation;
  DirectX::XMFLOAT4 rotation;
};

struct Animation {
  UINT numAnimatedBones;
  
  // bone id -> keyframes
  std::unordered_map<SHORT, std::vector<Keyframe>> bonesKeyframes;

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    fread(&numAnimatedBones, sizeof(numAnimatedBones), 1, fp);

    std::vector<UINT> numsKeyframes;
    numsKeyframes.resize(numAnimatedBones);

    fread(numsKeyframes.data(), sizeof(UINT), numAnimatedBones, fp);

    // TODO
  }
};

struct Subset {
  uint32_t start, count, vstart, pad;
  TEXTURENAME name;

  struct Texture* texture = nullptr;  // GPU data
};

template <typename T>
struct Mesh3D {
  struct {
    uint32_t numVerts;
    uint32_t numIndices;
    uint32_t numSubsets;
  } header;

  static_assert(std::is_base_of_v<Vertex, T>);
  std::vector<T> vertices;
  std::vector<uint16_t> indices;
  std::vector<Subset> subsets;

  std::string name;
  struct Geometry* geometry = nullptr;  // GPU data
  struct Skin* skin = nullptr; // in case of skinned mesh

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    name = filename;

    fread(&header, sizeof(header), 1, fp);

    vertices.resize(header.numVerts);
    indices.resize(header.numIndices);
    subsets.resize(header.numSubsets);

    //if constexpr (std::is_same_v<T, SkinnedVertex>) {

    //}

    fread(vertices.data(), sizeof(T), header.numVerts, fp);
    fread(indices.data(), sizeof(uint16_t), header.numIndices, fp);
    fread(subsets.data(), sizeof(Subset), header.numSubsets, fp);

    fclose(fp);
  }

  uint64_t VertexBufferSize() const { return sizeof(T) * header.numVerts; }

  uint64_t IndexBufferSize() const
  {
    return sizeof(uint16_t) * header.numIndices;
  }
};

struct Model3D {
  std::vector<Mesh3D<Vertex>*> meshes;
  std::vector<Mesh3D<SkinnedVertex>*> skinnedMeshes;
  // TODO: skeleton
  // TODO: std::vector<animations>

  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 translate;
  DirectX::XMFLOAT3 rotate;
  bool dirty;  // world position changed. Rename to collisionDirty?

  Model3D();
  DirectX::XMMATRIX WorldMatrix();

  void Scale(float s);
  void Rotate(float x, float y, float z);
  void Translate(float x, float y, float z);
  void Clean() { dirty = false;  }
};
