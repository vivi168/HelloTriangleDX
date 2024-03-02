#include "stdafx.h"
#include "Camera.h"
#include "Input.h"

#include <limits>

using namespace DirectX;

Camera::Camera()
{
  yaw = -XM_PIDIV2;
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

  const XMVECTOR worldUp = XMVectorSet(0.f, 1.f, 0.f, 0.f);

  XMVECTOR front = XMVector3Normalize(f);

  XMVECTOR r = XMVector3Normalize(XMVector3Cross(front, worldUp));
  XMStoreFloat3(&right, r);

  XMVECTOR fw = XMVector3Normalize(XMVector3Cross(worldUp, r));
  XMStoreFloat3(&forward, fw);

  XMVECTOR u = XMVector3Normalize(XMVector3Cross(r, front));

  XMVECTOR p = XMLoadFloat3(&translate);

  return XMMatrixLookAtLH(p, XMVectorAdd(p, front), u);
}

void Camera::Translate(float x, float y, float z) { translate = {x, y, z}; }

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

  constexpr float epsilon = std::numeric_limits<float>::epsilon();
  constexpr float upper = XM_PIDIV2 - epsilon;
  constexpr float lower = -XM_PIDIV2 + epsilon;

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
    translate.x += right.x * speed;
    translate.z += right.z * speed;
  }

  if (Input::IsHeld(Input::KB::D)) {
    translate.x -= right.x * speed;
    translate.z -= right.z * speed;
  }

  if (Input::IsHeld(Input::KB::Q)) {
    translate.y -= speed;
  }

  if (Input::IsHeld(Input::KB::E)) {
    translate.y += speed;
  }
}

std::string Camera::DebugString()
{
  char buff[512] = {0};
  sprintf_s(buff, "x: %f y: %f z: %f\nyaw: %f\npitch: %f", translate.x,
            translate.y, translate.z, yaw, pitch);

  return std::string(buff);
}
