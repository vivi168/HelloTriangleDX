#include "stdafx.h"

#include "Mesh.h"
#include "Renderer.h"

using namespace DirectX;

static XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

void Skin::Read(std::filesystem::path filename)
{
  FILE* fp;
  fopen_s(&fp, filename.string().c_str(), "rb");
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
  fread(inverseBindMatrices.data(), sizeof(XMFLOAT4X4), header.numJoints, fp);
}

void Skin::ReadStaticTransforms(std::filesystem::path filename)
{
  FILE* fp;
  fopen_s(&fp, filename.string().c_str(), "rb");
  assert(fp);

  UINT numBones;
  fread(&numBones, sizeof(numBones), 1, fp);

  std::vector<int> boneIds(numBones);
  fread(boneIds.data(), sizeof(int), numBones, fp);

  for (auto i : boneIds) {
    struct {
      XMFLOAT3 scale;
      XMFLOAT3 translation;
      XMFLOAT4 rotation;
    } transform;

    fread(&transform, sizeof(transform), 1, fp);

    XMVECTOR scale = XMLoadFloat3(&transform.scale);
    XMVECTOR trans = XMLoadFloat3(&transform.translation);
    XMVECTOR rot = XMLoadFloat4(&transform.rotation);

    auto issou = XMMatrixAffineTransformation(scale, zero, rot, trans);
    XMStoreFloat4x4(&staticTransforms[i], issou);
  }
}

void Animation::Read(std::filesystem::path filename)
{
  FILE* fp;
  fopen_s(&fp, filename.string().c_str(), "rb");
  assert(fp);

  UINT numAnimatedBones;

  fread(&numAnimatedBones, sizeof(numAnimatedBones), 1, fp);

  for (int i = 0; i < numAnimatedBones; i++) {
    int info[2];  // boneId + numKeyframes
    fread(info, sizeof(int), 2, fp);

    bonesKeyframes[info[0]].resize(info[1]);
    fread(bonesKeyframes[info[0]].data(), sizeof(Keyframe), info[1], fp);

    auto [minEl, maxEl] = std::minmax_element(bonesKeyframes[info[0]].begin(), bonesKeyframes[info[0]].end(),
                                              [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });

    if (minEl->time < minTime) minTime = minEl->time;
    if (maxEl->time > maxTime) maxTime = maxEl->time;
  }
}

XMMATRIX Animation::Interpolate(float curTime, int boneId, Skin* skin)
{
  auto it = bonesKeyframes.find(boneId);
  if (it == std::end(bonesKeyframes)) {
    return XMLoadFloat4x4(&skin->staticTransforms[boneId]);
  }

  auto keyframes = it->second;

  float startTime = keyframes.front().time;
  float endTime = keyframes.back().time;

  if (curTime <= startTime) {
    XMVECTOR scale = XMLoadFloat3(&keyframes.front().scale);
    XMVECTOR trans = XMLoadFloat3(&keyframes.front().translation);
    XMVECTOR rot = XMLoadFloat4(&keyframes.front().rotation);

    return XMMatrixAffineTransformation(scale, zero, rot, trans);
  } else if (curTime >= endTime) {
    XMVECTOR scale = XMLoadFloat3(&keyframes.back().scale);
    XMVECTOR trans = XMLoadFloat3(&keyframes.back().translation);
    XMVECTOR rot = XMLoadFloat4(&keyframes.back().rotation);

    return XMMatrixAffineTransformation(scale, zero, rot, trans);
  } else {
    for (size_t i = 0; i < keyframes.size(); i++) {
      if (curTime >= keyframes[i].time && curTime <= keyframes[i + 1].time) {
        float lerp = (curTime - keyframes[i].time) / (keyframes[i + 1].time - keyframes[i].time);

        XMVECTOR s1 = XMLoadFloat3(&keyframes[i].scale);
        XMVECTOR s2 = XMLoadFloat3(&keyframes[i + 1].scale);

        XMVECTOR t1 = XMLoadFloat3(&keyframes[i].translation);
        XMVECTOR t2 = XMLoadFloat3(&keyframes[i + 1].translation);

        XMVECTOR r1 = XMLoadFloat4(&keyframes[i].rotation);
        XMVECTOR r2 = XMLoadFloat4(&keyframes[i + 1].rotation);

        XMVECTOR scale = XMVectorLerp(s1, s2, lerp);
        XMVECTOR trans = XMVectorLerp(t1, t2, lerp);
        XMVECTOR rot = XMQuaternionSlerp(r1, r2, lerp);

        return XMMatrixAffineTransformation(scale, zero, rot, trans);
      }
    }
  }

  return XMMatrixIdentity();
}

std::vector<XMFLOAT4X4> Animation::BoneTransforms(float curTime,
                                                  Skin* skin,
                                                  std::unordered_map<int, XMMATRIX>& globalTransforms)
{
  globalTransforms[skin->header.rootBone] = Interpolate(curTime, skin->header.rootBone, skin);

  std::stack<int> stack;
  stack.push(skin->header.rootBone);

  while (!stack.empty()) {
    int bone = stack.top();
    stack.pop();

    auto parentGlobalTransform = globalTransforms[bone];

    auto children = skin->boneHierarchy[bone];
    for (auto child : children) {
      auto localTransform = Interpolate(curTime, child, skin);
      globalTransforms[child] = localTransform * parentGlobalTransform;
      stack.push(child);
    }
  }

  std::vector<XMFLOAT4X4> boneTransforms(skin->header.numJoints);

  for (size_t i = 0; i < skin->header.numJoints; i++) {
    auto joint = skin->jointIndices[i];
    XMMATRIX inverseBindMatrix = XMLoadFloat4x4(&skin->inverseBindMatrices[i]);

    XMMATRIX boneTransform = XMMatrixTranspose(inverseBindMatrix * globalTransforms[joint]);
    XMStoreFloat4x4(&boneTransforms[i], boneTransform);
  }

  return boneTransforms;
}

void Mesh3D::Read(std::filesystem::path filename, bool skinned)
{
  FILE* fp;
  fopen_s(&fp, filename.string().c_str(), "rb");
  assert(fp);

  name = filename;

  fread(&header, sizeof(header), 1, fp);

  indices.resize(header.numIndices);
  fread(indices.data(), sizeof(uint32_t), header.numIndices, fp);

  {
    struct TmpSubset {
      uint32_t start, count;
      FILENAME materialName;
    };

    std::vector<TmpSubset> tmpSubsets;
    subsets.resize(header.numSubsets);
    tmpSubsets.resize(header.numSubsets);
    fread(tmpSubsets.data(), sizeof(TmpSubset), header.numSubsets, fp);

    std::filesystem::path baseDir = name.parent_path();

    for (size_t i = 0; i < header.numSubsets; i++) {
      subsets[i].start = tmpSubsets[i].start;
      subsets[i].count = tmpSubsets[i].count;
      subsets[i].materialIndex = Renderer::CreateMaterial(baseDir, std::wstring(tmpSubsets[i].materialName));
    }
  }

  positions.resize(header.numVerts);
  normals.resize(header.numVerts);
  tangents.resize(header.numVerts);
  uvs.resize(header.numVerts);

  fread(positions.data(), sizeof(positions[0]), header.numVerts, fp);
  fread(normals.data(), sizeof(normals[0]), header.numVerts, fp);
  fread(uvs.data(), sizeof(uvs[0]), header.numVerts, fp);
  if (skinned) {
    blendWeightsAndIndices.resize(header.numVerts);
    fread(blendWeightsAndIndices.data(), sizeof(blendWeightsAndIndices[0]), header.numVerts, fp);
  }

  fread(&parentBone, sizeof(int), 1, fp);

  struct {
    XMFLOAT3 scale;
    XMFLOAT3 translation;
    XMFLOAT4 rotation;
  } transform;

  fread(&transform, sizeof(transform), 1, fp);
  XMVECTOR scale = XMLoadFloat3(&transform.scale);
  XMVECTOR trans = XMLoadFloat3(&transform.translation);
  XMVECTOR rot = XMLoadFloat4(&transform.rotation);

  auto issou = XMMatrixAffineTransformation(scale, XMVectorZero(), rot, trans);
  XMStoreFloat4x4(&localTransform, issou);

  fclose(fp);

  ComputeAdditionalData();

  wprintf(
      L"=== %s ===\nnumVerts: %d\nnumIndices: %d\nnumMeshlets: %d\nnumUniqueVertexIndices: %d\nnumPrimitives: %d\n\n",
      name.wstring().c_str(), header.numVerts, header.numIndices, meshlets.size(), uniqueVertexIndices.size(),
      primitiveIndices.size());
}

// TODO: cache all this
// check if meshlet data file is older than mesh file. if so regenerate. if not, load from disk.
void Mesh3D::ComputeAdditionalData()
{
  BoundingSphere::CreateFromPoints(boundingSphere, positions.size(), positions.data(), sizeof(positions[0]));

  ComputeTangentFrame(indices.data(), indices.size() / 3, positions.data(), normals.data(), uvs.data(), header.numVerts,
                      tangents.data());

  std::vector<Meshlet> dxMeshlets;

  // Meshlet generation
  {
    using MeshletSubset = std::pair<size_t, size_t>;  // start, count

    std::vector<MeshletSubset> meshSubsets;
    meshSubsets.reserve(header.numSubsets);

    for (const auto& subset : subsets) {
      meshSubsets.emplace_back(subset.start / 3, subset.count / 3);
    }

    std::vector<MeshletSubset> meshletSubsets(header.numSubsets);

    constexpr size_t maxPrims = MESHLET_MAX_PRIM;
    constexpr size_t maxVerts = MESHLET_MAX_VERT;
    CHECK_HR(ComputeMeshlets(indices.data(), indices.size() / 3, positions.data(), positions.size(), meshSubsets.data(),
                             meshSubsets.size(), nullptr, dxMeshlets, uniqueVertexIndices, primitiveIndices,
                             meshletSubsets.data(), maxVerts, maxPrims));

    assert(uniqueVertexIndices.size() % 4 == 0);

    meshlets.resize(dxMeshlets.size());
    for (size_t i = 0; i < dxMeshlets.size(); i++) {
      meshlets[i].numVerts = dxMeshlets[i].VertCount;
      meshlets[i].firstVert = dxMeshlets[i].VertOffset;
      meshlets[i].numPrims = dxMeshlets[i].PrimCount;
      meshlets[i].firstPrim = dxMeshlets[i].PrimOffset;
    }

    for (uint32_t i = 0; i < meshletSubsets.size(); i++) {
      auto start = meshletSubsets[i].first;
      auto end = start + meshletSubsets[i].second;

      for (size_t j = start; j < end; j++) {
        meshlets[j].materialIndex = subsets[i].materialIndex;
      }
    }
  }

  // Meshlet cull data generation
  {
    std::vector<CullData> cullData;
    cullData.resize(dxMeshlets.size());
    CHECK_HR(ComputeCullData(positions.data(), positions.size(), dxMeshlets.data(), dxMeshlets.size(),
                             reinterpret_cast<uint32_t*>(uniqueVertexIndices.data()), uniqueVertexIndices.size(),
                             primitiveIndices.data(), primitiveIndices.size(), cullData.data(), MESHLET_DEFAULT));

    for (size_t i = 0; i < meshlets.size(); i++) {
      meshlets[i].boundingSphere = cullData[i].BoundingSphere;
      meshlets[i].normalCone = cullData[i].NormalCone;
      meshlets[i].apexOffset = cullData[i].ApexOffset;
    }
  }
}

Model3D& Model3D::Read(std::filesystem::path filename)
{
  // TODO: add this as constexpr in stdafx.h or something and add an helper function
  std::filesystem::path basePath = "assets";

  std::ifstream file(basePath / filename);
  std::string line;

  std::getline(file, line);
  std::string baseDir = line.substr(line.find(":") + 2);
  std::filesystem::path dir = basePath / baseDir;

  std::getline(file, line);
  size_t numMesh = std::stoi(line.substr(line.find(":") + 1));

  std::getline(file, line);
  size_t numSkinnedMesh = std::stoi(line.substr(line.find(":") + 1));

  std::getline(file, line);
  size_t numAnimations = std::stoi(line.substr(line.find(":") + 1));

  std::getline(file, line);
  std::optional<std::filesystem::path> staticTransform;
  std::string transformFile = line.substr(line.find(":") + 2);
  if (transformFile != "None") staticTransform = dir / transformFile;

  for (size_t i = 0; i < numMesh; ++i) {
    std::getline(file, line);
    AddMesh(dir / line);
  }

  for (size_t i = 0; i < numSkinnedMesh; ++i) {
    std::getline(file, line);
    auto sep = line.find(';');
    std::string mesh = line.substr(0, sep);
    std::string skin = line.substr(sep + 1);

    AddSkinnedMesh(dir / mesh, dir / skin, staticTransform);
  }

  for (size_t i = 0; i < numAnimations; ++i) {
    std::getline(file, line);
    auto sep = line.find(';');
    std::string anim = line.substr(0, sep);
    std::string name = line.substr(sep + 1);

    AddAnimation(dir / anim, name);
  }

  return *this;
}

XMMATRIX Model3D::WorldMatrix() const
{
  XMVECTOR scaleVector = XMLoadFloat3(&scale);
  XMVECTOR transVector = XMLoadFloat3(&translate);
  XMVECTOR rotVector = XMLoadFloat3(&rotate);
  XMVECTOR q = XMQuaternionRotationRollPitchYawFromVector(rotVector);

  return XMMatrixAffineTransformation(scaleVector, zero, q, transVector);
}
