#pragma once

#include <string>
#include <vector>

#include "DirectXMesh.h"

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
    }
  }

  DirectX::XMMATRIX Interpolate(float curTime, int boneId, Skin* skin);

  std::vector<DirectX::XMFLOAT4X4> BoneTransforms(float curTime, Skin* skin);
};

struct AnimationInfo {
  Animation* animation = nullptr;

  float curTime = 0.0f;

  std::vector<DirectX::XMFLOAT4X4> BoneTransforms(float dt, Skin* skin)
  {
    curTime += dt;
    if (curTime > animation->maxTime) curTime = animation->minTime;

    return animation->BoneTransforms(curTime, skin);
  }
};

// Forward declaration
namespace Renderer
{
struct Texture;
}  // namespace Renderer

struct Subset {
  uint32_t start, count, vstart, pad;
  TEXTURENAME name;

  Renderer::Texture* texture = nullptr;  // GPU data // TODO Material struct instead
};

using MeshletSubset = std::pair<size_t, size_t>; // offset, count

template <typename T>
struct Mesh3D {
  struct {
    uint32_t numVerts;
    uint32_t numIndices;
    uint32_t numSubsets;
  } header;

  static_assert(std::is_base_of_v<Vertex, T>);
  std::vector<uint16_t> indices; // TODO: TMP export to uint32_t
  std::vector<Subset> subsets;

  std::vector<DirectX::XMFLOAT3> positions;
  std::vector<DirectX::XMFLOAT3> normals;
  std::vector<DirectX::XMFLOAT2> uvs;
  std::vector<DirectX::XMUINT2> blendWeightsAndIndices;

  // mesh shader specific
  std::vector<DirectX::Meshlet> meshlets;
  std::vector<uint8_t> uniqueVertexIndices; // underlying indices are uint32
  std::vector<DirectX::MeshletTriangle> primitiveIndices;
  std::vector<Subset*> meshletSubsetIndices; // map meshlet -> subset

  std::string name;
  Skin* skin = nullptr;  // in case of skinned mesh

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    name = filename;

    fread(&header, sizeof(header), 1, fp);

    positions.resize(header.numVerts);
    normals.resize(header.numVerts);
    uvs.resize(header.numVerts);
    if constexpr (std::is_same_v<T, SkinnedVertex>) {
      blendWeightsAndIndices.resize(header.numVerts);
    }
    indices.resize(header.numIndices);
    subsets.resize(header.numSubsets);

    fread(positions.data(), sizeof(positions[0]), header.numVerts, fp);
    fread(normals.data(), sizeof(normals[0]), header.numVerts, fp);
    fread(uvs.data(), sizeof(uvs[0]), header.numVerts, fp);
    if constexpr (std::is_same_v<T, SkinnedVertex>) {
      fread(blendWeightsAndIndices.data(), sizeof(blendWeightsAndIndices[0]), header.numVerts, fp);
    }
    fread(indices.data(), sizeof(uint16_t), header.numIndices, fp);
    fread(subsets.data(), sizeof(Subset), header.numSubsets, fp);

    // TODO: build bounding sphere
    // TODO: move this out of runtime
    {
      // TODO: modify gltf.py export to have 32 bits indices.
      std::vector<uint32_t> indices32(indices.size());
      std::vector<MeshletSubset> subsets_st(subsets.size());

      for (size_t si = 0; si < subsets.size(); si++) {
        const auto& subset = subsets[si];
        subsets_st[si] = std::make_pair<size_t, size_t>(subset.start / 3, subset.count / 3);

        for (uint32_t i = 0; i < subset.count; i++) {
          uint32_t gi = subset.start + i;

          indices32[gi] = static_cast<uint32_t>(indices[gi]) + subset.vstart;
          assert(indices32[gi] < header.numVerts);
        }
      }

      std::vector<MeshletSubset> meshletSubsets(header.numSubsets);

      constexpr size_t maxPrims = 124;
      constexpr size_t maxVerts = 64;
      CHECK_HR(ComputeMeshlets(
          indices32.data(), indices32.size() / 3,
          positions.data(), header.numVerts,
          subsets_st.data(), subsets_st.size(),
          nullptr,
          meshlets, uniqueVertexIndices, primitiveIndices,
          meshletSubsets.data(), maxVerts, maxPrims));

      assert(uniqueVertexIndices.size() % 4 == 0);

      meshletSubsetIndices.resize(meshlets.size());
      for (uint32_t i = 0; i < meshletSubsets.size(); i++) {
        auto start = meshletSubsets[i].first;
        auto end = start + meshletSubsets[i].second;

        for (uint32_t j = start; j < end; j++) {
          // TODO: this is dangerous. if subsets changes (push_back(),...) all pointers are invalidated.
          // instead store an index ? if we do this offline, we won't have a choice
          // if we do it offline, add a fifth member to Meshlet struct?
          meshletSubsetIndices[j] = &subsets[i];
        }
      }
    }

    fclose(fp);
  }

  size_t PositionsBufferSize() const { return sizeof(positions[0]) * header.numVerts; }

  size_t NormalsBufferSize() const { return sizeof(normals[0]) * header.numVerts; }

  size_t UvsBufferSize() const { return sizeof(uvs[0]) * header.numVerts; }

  size_t BlendWeightsAndIndicesBufferSize() const { return sizeof(blendWeightsAndIndices[0]) * header.numVerts; }

  size_t IndexBufferSize() const { return sizeof(uint16_t) * header.numIndices; }

  size_t MeshletBufferSize() const { return sizeof(DirectX::Meshlet) * meshlets.size(); }

  size_t MeshletIndexBufferNumElements() const { return DivRoundUp(uniqueVertexIndices.size(), sizeof(UINT)); }

  size_t MeshletIndexBufferSize() const { return MeshletIndexBufferNumElements() * sizeof(UINT); }

  size_t MeshletPrimitiveBufferSize() const { return sizeof(DirectX::MeshletTriangle) * primitiveIndices.size(); }

  size_t SkinMatricesBufferSize() const
  {
    if (!skin) return 0;

    return sizeof(DirectX::XMFLOAT4X4) * skin->header.numJoints;
  }

  size_t SkinMatricesSize() const
  {
    if (!skin) return 0;

    return skin->header.numJoints;
  }
};

struct Model3D {
  std::vector<Mesh3D<Vertex>*> meshes;
  std::vector<Mesh3D<SkinnedVertex>*> skinnedMeshes;
  std::vector<Skin*> skins; // TODO: loop this and not meshes then skin when computing matrices.
  std::unordered_map<std::string, Animation*> animations;
  AnimationInfo currentAnimation;

  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 translate;
  DirectX::XMFLOAT3 rotate;
  bool dirty;  // world position changed. Rename to collisionDirty?

  Model3D();
  DirectX::XMMATRIX WorldMatrix();

  void Scale(float s);
  void Rotate(float x, float y, float z);
  void Translate(float x, float y, float z);

  void SetCurrentAnimation(std::string name)
  {
    currentAnimation.animation = animations[name];
    assert(currentAnimation.animation);
  }

  bool HasCurrentAnimation() { return currentAnimation.animation != nullptr; }

  void Clean() { dirty = false; }
};
