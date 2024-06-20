#include "stdafx.h"
#include "Camera.h"
#include "Input.h"

#include <limits>

using namespace DirectX;

static constexpr float epsilon = std::numeric_limits<float>::epsilon();
static constexpr float upper = XM_PIDIV2 - epsilon;
static constexpr float lower = -XM_PIDIV2 + epsilon;

const XMVECTOR Camera::worldUp = XMVectorSet(0.f, 1.f, 0.f, 0.f);

Camera::Camera()
{
  yaw = XM_PIDIV2;
  pitch = 0;
  speed = 1.0f;
  sensitivity = 0.01f;

  translate = {0, 0, 0};

  forward = {0, 0, 0};
  right = {0, 0, 0};
}

XMMATRIX Camera::LookAt()
{
  const XMVECTOR f = XMVectorSet(cosf(yaw) * cosf(pitch), sinf(pitch),
                                 sinf(yaw) * cosf(pitch), 0.f);

  XMVECTOR front = XMVector3Normalize(f);

  XMVECTOR r = XMVector3Normalize(XMVector3Cross(worldUp, front));
  XMStoreFloat3(&right, r);

  XMVECTOR fw = XMVector3Normalize(XMVector3Cross(r, worldUp));
  XMStoreFloat3(&forward, fw);

  XMVECTOR position = XMLoadFloat3(&translate);

  return XMMatrixLookAtLH(position, XMVectorAdd(position, front), worldUp);
}

void Camera::Translate(float x, float y, float z) { translate = {x, y, z}; }

void Camera::Target(float x, float y, float z)
{
  XMVECTOR p = XMLoadFloat3(&translate);
  XMVECTOR t = XMVectorSet(x, y, z, 0.0f);
  XMVECTOR d = XMVector3Normalize(t - p);
  XMFLOAT3 dir;
  XMStoreFloat3(&dir, d);

  yaw = atan2f(dir.z, dir.x);
  pitch = asinf(dir.y);
}

void Camera::Orient(float p, float y)
{
  //pitch = p;
  yaw = y;
}

void Camera::ProcessKeyboard()
{
  if (Input::IsHeld(Input::KB::Up)) {
    pitch += sensitivity;
  }
  if (Input::IsHeld(Input::KB::Down)) {
    pitch -= sensitivity;
  }
  if (Input::IsHeld(Input::KB::Left)) {
    yaw += sensitivity;
  }
  if (Input::IsHeld(Input::KB::Right)) {
    yaw -= sensitivity;
  }

  if (pitch > upper)
    pitch = upper;
  else if (pitch < lower)
    pitch = lower;

  if (Input::IsHeld(Input::KB::W)) {
    translate.x += forward.x * speed;
    translate.z += forward.z * speed;
  }

  if (Input::IsHeld(Input::KB::S)) {
    translate.x -= forward.x * speed;
    translate.z -= forward.z * speed;
  }

  if (Input::IsHeld(Input::KB::A)) {
    translate.x -= right.x * speed;
    translate.z -= right.z * speed;
  }

  if (Input::IsHeld(Input::KB::D)) {
    translate.x += right.x * speed;
    translate.z += right.z * speed;
  }

  if (Input::IsHeld(Input::KB::Q)) {
    translate.y -= speed;
  }

  if (Input::IsHeld(Input::KB::E)) {
    translate.y += speed;
  }
}

void Camera::DebugWindow()
{
  ImGui::Begin("Camera details");
  ImGui::Text("x: %f y: %f z: %f\nyaw: %f", translate.x, translate.y,
              translate.z, yaw);
  ImGui::SliderFloat("pitch", &pitch, lower, upper);
  ImGui::End();
}
