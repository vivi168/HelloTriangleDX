#include "stdafx.h"
#include "Mesh.h"

#include <stack>

using namespace DirectX;

XMMATRIX Animation::Interpolate(int boneId)
{
  auto keyframes = bonesKeyframes[boneId];

  float startTime = keyframes.front().time;
  float endTime = keyframes.back().time;

  XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

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

std::vector<XMFLOAT4X4> Animation::BoneTransforms(Skin* skin)
{
  std::unordered_map<int, XMMATRIX> boneLocalTransforms;
  boneLocalTransforms[skin->header.rootBone] = Interpolate(skin->header.rootBone);

  std::stack<int> stack;
  stack.push(skin->header.rootBone);

  while (!stack.empty()) {
    int bone = stack.top();
    stack.pop();

    auto parentLocalTransform = boneLocalTransforms[bone];

    auto children = skin->boneHierarchy[bone];
    for (auto child : children) {
      auto localTransform = Interpolate(child);
      boneLocalTransforms[child] = localTransform * parentLocalTransform;
      stack.push(child);
    }
  }

  std::vector<XMFLOAT4X4> boneTransforms(skin->header.numJoints);

  for (size_t i = 0; i < skin->header.numJoints; i++) {
    auto joint = skin->jointIndices[i];
    XMMATRIX inverseBindMatrix = XMLoadFloat4x4(&skin->inverseBindMatrices[i]);

    XMMATRIX boneTransform = XMMatrixTranspose(inverseBindMatrix * boneLocalTransforms[joint]);
    XMStoreFloat4x4(&boneTransforms[i], boneTransform);
  }

  return boneTransforms;
}

Model3D::Model3D()
    : scale(1.f, 1.f, 1.f),
      translate(0.f, 0.f, 0.f),
      rotate(0.f, 0.f, 0.f),
      dirty(false)
{
}

XMMATRIX Model3D::WorldMatrix()
{
  XMVECTOR scaleVector = XMLoadFloat3(&scale);
  XMVECTOR transVector = XMLoadFloat3(&translate);
  XMVECTOR rotVector = XMLoadFloat3(&rotate);
  XMVECTOR q = XMQuaternionRotationRollPitchYawFromVector(rotVector);

  XMMATRIX scaleMatrix = XMMatrixScalingFromVector(scaleVector);
  XMMATRIX transMatrix = XMMatrixTranslationFromVector(transVector);
  XMMATRIX rotMatrix = XMMatrixRotationQuaternion(q);

  return scaleMatrix * rotMatrix * transMatrix;
}

void Model3D::Scale(float s)
{
  scale = {s, s, s};
  dirty = true;
}

void Model3D::Rotate(float x, float y, float z)
{
  rotate = {x, y, z};
  dirty = true;
}

void Model3D::Translate(float x, float y, float z)
{
  translate = {x, y, z};
  dirty = true;
}
