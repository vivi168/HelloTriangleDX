#pragma once

#include <string>
#include <vector>

#include "DirectXMesh.h"

#define MAX_TEXTURE_NAME_LEN MAX_PATH
typedef WCHAR TEXTURENAME[MAX_TEXTURE_NAME_LEN];

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
  std::shared_ptr<Animation> animation = nullptr;

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
  uint32_t start, count;
  TEXTURENAME name;

  Renderer::Texture* texture = nullptr;  // GPU data // TODO Material struct instead
};

using MeshletSubset = std::pair<size_t, size_t>; // start, count

struct Mesh3D {
  struct {
    uint32_t numVerts;
    uint32_t numIndices;
    uint32_t numSubsets;
  } header;

  std::vector<uint32_t> indices;
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

  std::wstring name;
  std::shared_ptr<Skin> skin = nullptr;  // in case of skinned mesh

  void Read(std::wstring filename, bool skinned = false)
  {
    FILE* fp;
    _wfopen_s(&fp, filename.c_str(), L"rb");
    assert(fp);

    name = filename;

    fread(&header, sizeof(header), 1, fp);

    positions.resize(header.numVerts);
    normals.resize(header.numVerts);
    uvs.resize(header.numVerts);
    indices.resize(header.numIndices);
    subsets.resize(header.numSubsets);

    fread(indices.data(), sizeof(uint32_t), header.numIndices, fp);
    fread(subsets.data(), sizeof(Subset), header.numSubsets, fp);

    fread(positions.data(), sizeof(positions[0]), header.numVerts, fp);
    fread(normals.data(), sizeof(normals[0]), header.numVerts, fp);
    fread(uvs.data(), sizeof(uvs[0]), header.numVerts, fp);
    if (skinned) {
      blendWeightsAndIndices.resize(header.numVerts);
      fread(blendWeightsAndIndices.data(), sizeof(blendWeightsAndIndices[0]), header.numVerts, fp);
    }

    // TODO: build bounding sphere
    // TODO: move this out of runtime
    {
      std::vector<MeshletSubset> meshSubsets;
      meshSubsets.reserve(header.numSubsets);

      for (const auto& subset : subsets) {
        meshSubsets.emplace_back(subset.start / 3, subset.count / 3);
      }

      std::vector<MeshletSubset> meshletSubsets(header.numSubsets);

      constexpr size_t maxPrims = 126;
      constexpr size_t maxVerts = 64;
      CHECK_HR(ComputeMeshlets(
          indices.data(), indices.size() / 3,
          positions.data(), header.numVerts,
          meshSubsets.data(), meshSubsets.size(),
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

  bool Skinned() const { return skin != nullptr; }

  size_t PositionsBufferSize() const { return sizeof(positions[0]) * header.numVerts; }

  size_t NormalsBufferSize() const { return sizeof(normals[0]) * header.numVerts; }

  size_t UvsBufferSize() const { return sizeof(uvs[0]) * header.numVerts; }

  size_t BlendWeightsAndIndicesBufferSize() const { return sizeof(blendWeightsAndIndices[0]) * header.numVerts; }

  size_t MeshletBufferSize() const { return sizeof(DirectX::Meshlet) * meshlets.size(); }

  size_t MeshletIndexBufferNumElements() const { return DivRoundUp(uniqueVertexIndices.size(), sizeof(UINT)); }

  size_t MeshletIndexBufferSize() const { return MeshletIndexBufferNumElements() * sizeof(UINT); }

  size_t MeshletPrimitiveBufferSize() const { return sizeof(DirectX::MeshletTriangle) * primitiveIndices.size(); }

  size_t SkinMatricesBufferSize() const
  {
    if (!Skinned()) return 0;

    return sizeof(DirectX::XMFLOAT4X4) * skin->header.numJoints;
  }

  size_t SkinMatricesSize() const
  {
    if (!Skinned()) return 0;

    return skin->header.numJoints;
  }
};

struct Model3D {
  std::vector<std::shared_ptr<Mesh3D>> meshes;
  std::unordered_map<std::string, std::shared_ptr<Skin>> skins;
  std::unordered_map<std::string, std::shared_ptr<Animation>> animations;

  AnimationInfo currentAnimation;
  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 translate;
  DirectX::XMFLOAT3 rotate;
  bool dirty;  // world position changed. Rename to collisionDirty?

  Model3D();

  Model3D& Read(std::string filename)
  {
    std::filesystem::path basePath = "assets";

    std::ifstream file((basePath / filename).string());
    std::string line;

    std::getline(file, line);
    std::string baseDir = line.substr(line.find(":") + 2);

    std::getline(file, line);
    size_t numMesh = std::stoi(line.substr(line.find(":") + 1));

    std::getline(file, line);
    size_t numSkinnedMesh = std::stoi(line.substr(line.find(":") + 1));

    std::getline(file, line);
    size_t numAnimations = std::stoi(line.substr(line.find(":") + 1));

    std::getline(file, line);
    std::optional<std::string> staticTransform;
    std::string transformFile = line.substr(line.find(":") + 2);
    if (transformFile != "None") staticTransform = (basePath / baseDir / transformFile).string();

    for (size_t i = 0; i < numMesh; ++i) {
      std::getline(file, line);
      AddMesh((basePath / baseDir / line).wstring());
    }

    for (size_t i = 0; i < numSkinnedMesh; ++i) {
      std::getline(file, line);
      auto sep = line.find(';');
      std::string mesh = line.substr(0, sep);
      std::string skin = line.substr(sep + 1);

      AddSkinnedMesh((basePath / baseDir / mesh).wstring(), (basePath / baseDir / skin).string(), staticTransform);
    }

    for (size_t i = 0; i < numAnimations; ++i) {
      std::getline(file, line);
      auto sep = line.find(';');
      std::string anim = line.substr(0, sep);
      std::string name = line.substr(sep + 1);

      AddAnimation((basePath / baseDir / anim).string(), name);
    }

    return *this;
  }

  Model3D SpawnInstance() const
  {
    Model3D instance;
    instance.meshes = this->meshes;
    instance.skins = this->skins;
    instance.animations = this->animations;

    return instance;
  }

  Model3D& AddMesh(std::wstring filename)
  {
    auto mesh = std::make_unique<Mesh3D>();
    mesh->Read(filename);

    meshes.push_back(std::move(mesh));

    return *this;
  }

  Model3D& AddSkinnedMesh(std::wstring meshFilename, std::string skinFilename,
                          std::optional<std::string> transformFilename = std::nullopt)
  {
    auto mesh = std::make_shared<Mesh3D>();
    mesh->Read(meshFilename, true);

    auto it = skins.find(skinFilename);
    if (it == std::end(skins)) {
      auto skin = std::make_shared<Skin>();
      skin->Read(skinFilename);

      if (transformFilename.has_value()) {
        skin->ReadStaticTransforms(transformFilename.value());
      }

      mesh->skin = skin;
      skins[skinFilename] = skin;
    } else {
      mesh->skin = skins[skinFilename];
    }

    meshes.push_back(mesh);

    return *this;
  }

  Model3D& AddAnimation(std::string filename, std::string name)
  {
    auto animation = std::make_shared<Animation>();
    animation->Read(filename);

    animations[name] = animation;

    return *this;
  }

  Model3D& SetCurrentAnimation(std::string name)
  {
    currentAnimation.animation = animations[name];
    assert(currentAnimation.animation);

    return *this;
  }

  bool HasCurrentAnimation() { return currentAnimation.animation != nullptr; }

  DirectX::XMMATRIX WorldMatrix();

  Model3D& Scale(float s);
  Model3D& Rotate(float x, float y, float z);
  Model3D& Translate(float x, float y, float z);

  void Clean() { dirty = false; }
};
