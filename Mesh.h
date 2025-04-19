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
    int rootBone;
    UINT numBones;
    UINT numJoints;
  } header;

  // parent -> children
  std::unordered_map<int, std::vector<int>> boneHierarchy;
  std::vector<int> jointIndices;
  std::vector<DirectX::XMFLOAT4X4> inverseBindMatrices;

  std::unordered_map<int, DirectX::XMFLOAT4X4> staticTransforms;

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    fread(&header, sizeof(header), 1, fp);

    // bone hierarchy
    std::vector<int> childBones(header.numBones);
    std::vector<int> parentBones(header.numBones);

    fread(childBones.data(), sizeof(int), header.numBones, fp);
    fread(parentBones.data(), sizeof(int), header.numBones, fp);

    for (int i = 0; i < header.numBones; i++) {
      auto parent = parentBones[i];
      if (parent < 0) continue;
      boneHierarchy[parent].push_back(childBones[i]);
    }

    // joints + inverse bind matrices
    jointIndices.resize(header.numJoints);
    inverseBindMatrices.resize(header.numJoints);

    fread(jointIndices.data(), sizeof(int), header.numJoints, fp);
    fread(inverseBindMatrices.data(), sizeof(DirectX::XMFLOAT4X4),
          header.numJoints, fp);
  }

  void ReadStaticTransforms(std::string filename);
};

struct Keyframe {
  float time;
  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 translation;
  DirectX::XMFLOAT4 rotation;
};

struct Animation {
  // bone id -> keyframes
  std::unordered_map<int, std::vector<Keyframe>> bonesKeyframes;

  float minTime;
  float maxTime;
  float curTime = 0.0f;

  void Update(float dt) {
    curTime += dt;
    if (curTime > maxTime)
      curTime = minTime;
  }

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    UINT numAnimatedBones;

    fread(&numAnimatedBones, sizeof(numAnimatedBones), 1, fp);

    for (int i = 0; i < numAnimatedBones; i++) {
      int info[2];  // boneId + numKeyframes
      fread(info, sizeof(int), 2, fp);

      bonesKeyframes[info[0]].resize(info[1]);
      fread(bonesKeyframes[info[0]].data(), sizeof(Keyframe), info[1], fp);

      auto [minEl, maxEl] = std::minmax_element(
          bonesKeyframes[info[0]].begin(), bonesKeyframes[info[0]].end(),
          [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

      if (minEl->time < minTime) minTime = minEl->time;
      if (maxEl->time > maxTime) maxTime = maxEl->time;

      curTime = minTime;
    }
  }

  DirectX::XMMATRIX Interpolate(int boneId, Skin* skin);

  std::vector<DirectX::XMFLOAT4X4> BoneTransforms(Skin* skin);
};

// Forward declaration
namespace Renderer
{
struct Geometry;
struct Texture;
}  // namespace Renderer

struct Subset {
  uint32_t start, count, vstart, pad;
  TEXTURENAME name;

  Renderer::Texture* texture = nullptr;  // GPU data // TODO Material struct instead
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
  std::vector<uint32_t> augmentedIndices;
  std::vector<Subset> subsets;

  std::string name;
  Renderer::Geometry* geometry = nullptr;  // GPU data
  Skin* skin = nullptr;                 // in case of skinned mesh

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

    fread(vertices.data(), sizeof(T), header.numVerts, fp);
    fread(indices.data(), sizeof(uint16_t), header.numIndices, fp);
    fread(subsets.data(), sizeof(Subset), header.numSubsets, fp);

    if constexpr (std::is_same_v<T, Vertex>) {
      augmentedIndices.reserve(header.numIndices);

      for (auto& i : indices) {
        // POC: show that SV_VertexID value is indeed pulled from index buffer
        augmentedIndices.emplace_back(static_cast<uint32_t>(i << 16));
      }
    }

    fclose(fp);
  }

  size_t VertexBufferSize() const { return sizeof(T) * header.numVerts; }

  size_t IndexBufferSize() const
  {
    if constexpr (std::is_same_v<T, Vertex>) {
      return sizeof(uint32_t) * header.numIndices;
    }
    return sizeof(uint16_t) * header.numIndices;
  }

  size_t SkinMatricesSize() const {
    if (!skin) return 0;

    return sizeof(DirectX::XMFLOAT4X4) * skin->header.numJoints;
  }
};

struct Model3D {
  std::vector<Mesh3D<Vertex>*> meshes;
  std::vector<Mesh3D<SkinnedVertex>*> skinnedMeshes;
  std::vector<Skin*> skins; // TODO: loop this and not meshes then skin when computing matrices.
  std::unordered_map<std::string, Animation*> animations;
  Animation* currentAnimation = nullptr;

  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 translate;
  DirectX::XMFLOAT3 rotate;
  bool dirty;  // world position changed. Rename to collisionDirty?

  Model3D();
  DirectX::XMMATRIX WorldMatrix();

  void Scale(float s);
  void Rotate(float x, float y, float z);
  void Translate(float x, float y, float z);
  void Clean() { dirty = false; }
};
