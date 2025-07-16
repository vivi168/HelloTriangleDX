#pragma once

#include <string>
#include <vector>

#include "DirectXMesh.h"

typedef WCHAR FILENAME[MAX_PATH];

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

  void Read(std::filesystem::path filename);

  void ReadStaticTransforms(std::filesystem::path filename);
};

struct Animation {
  struct Keyframe {
    float time;
    DirectX::XMFLOAT3 scale;
    DirectX::XMFLOAT3 translation;
    DirectX::XMFLOAT4 rotation;
  };

  // bone id -> keyframes
  std::unordered_map<int, std::vector<Keyframe>> bonesKeyframes;

  float minTime;
  float maxTime;

  void Read(std::filesystem::path filename);

  DirectX::XMMATRIX Interpolate(float curTime, int boneId, Skin* skin);

  std::vector<DirectX::XMFLOAT4X4> BoneTransforms(float curTime,
                                                  Skin* skin,
                                                  std::unordered_map<int, DirectX::XMMATRIX>& globalTransforms);
};

struct AnimationInfo {
  std::shared_ptr<Animation> animation = nullptr;
  std::unordered_map<int, DirectX::XMMATRIX> globalTransforms;

  float curTime = 0.0f;

  std::vector<DirectX::XMFLOAT4X4> BoneTransforms(float dt, Skin* skin)
  {
    curTime += dt;
    if (curTime > animation->maxTime) curTime = animation->minTime;

    return animation->BoneTransforms(curTime, skin, globalTransforms);
  }
};

struct Subset {
  uint32_t start, count, materialIndex;
};

struct MeshletData {
  uint32_t numVerts;
  uint32_t firstVert;
  uint32_t numPrim;
  uint32_t firstPrim;

  uint32_t instanceIndex;
  uint32_t materialIndex;

  DirectX::BoundingSphere boundingSphere;       // xyz = center, w = radius
  DirectX::PackedVector::XMUBYTEN4 normalCone;  // xyz = axis, w = -cos(a + 90)
  float apexOffset;                             // apex = center - axis * offset
};

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
  std::vector<DirectX::XMFLOAT4> tangents;
  std::vector<DirectX::XMFLOAT2> uvs;
  std::vector<DirectX::XMUINT2> blendWeightsAndIndices;

  // mesh shader specific
  std::vector<MeshletData> meshlets;
  std::vector<uint8_t> uniqueVertexIndices;  // underlying indices are uint32, but stored in uint8_t array
  std::vector<DirectX::MeshletTriangle> primitiveIndices;

  DirectX::BoundingSphere boundingSphere;

  std::filesystem::path name;
  std::shared_ptr<Skin> skin = nullptr;  // in case of skinned mesh

  int parentBone = -1;
  DirectX::XMFLOAT4X4 localTransform;

  void Read(std::filesystem::path filename, bool skinned = false);

  void ComputeAdditionalData();

  bool Skinned() const { return skin != nullptr; }

  size_t PositionsBufferSize() const { return sizeof(positions[0]) * header.numVerts; }

  size_t NormalsBufferSize() const { return sizeof(normals[0]) * header.numVerts; }

  size_t UvsBufferSize() const { return sizeof(uvs[0]) * header.numVerts; }

  size_t BlendWeightsAndIndicesBufferSize() const { return sizeof(blendWeightsAndIndices[0]) * header.numVerts; }

  size_t MeshletBufferSize() const { return sizeof(MeshletData) * meshlets.size(); }

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

  DirectX::XMMATRIX LocalTransformMatrix() const { return DirectX::XMLoadFloat4x4(&localTransform); }
};

struct Model3D {
  std::vector<std::shared_ptr<Mesh3D>> meshes;
  std::unordered_map<std::wstring, std::shared_ptr<Skin>> skins;
  std::unordered_map<std::string, std::shared_ptr<Animation>> animations;

  AnimationInfo currentAnimation;
  DirectX::XMFLOAT3 scale;
  DirectX::XMFLOAT3 translate;
  DirectX::XMFLOAT3 rotate;
  bool dirty;  // world position changed. Rename to collisionDirty?

  Model3D() : scale(1.f, 1.f, 1.f), translate(0.f, 0.f, 0.f), rotate(0.f, 0.f, 0.f), dirty(false) {}

  Model3D& Read(std::filesystem::path filename);

  Model3D SpawnInstance() const
  {
    Model3D instance;
    instance.meshes = this->meshes;
    instance.skins = this->skins;
    instance.animations = this->animations;

    return instance;
  }

  Model3D& AddMesh(std::filesystem::path filename)
  {
    auto mesh = std::make_unique<Mesh3D>();
    mesh->Read(filename);

    meshes.push_back(std::move(mesh));

    return *this;
  }

  Model3D& AddSkinnedMesh(std::filesystem::path meshFilename,
                          std::filesystem::path skinFilename,
                          std::optional<std::filesystem::path> transformFilename = std::nullopt)
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

  Model3D& AddAnimation(std::filesystem::path filename, std::string name)
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

  DirectX::XMMATRIX WorldMatrix() const;

  Model3D& Scale(float s)
  {
    scale = {s, s, s};
    dirty = true;

    return *this;
  }

  Model3D& Rotate(float x, float y, float z)
  {
    rotate = {x, y, z};
    dirty = true;

    return *this;
  }

  Model3D& Translate(float x, float y, float z)
  {
    translate = {x, y, z};
    dirty = true;

    return *this;
  }

  void Clean() { dirty = false; }
};
