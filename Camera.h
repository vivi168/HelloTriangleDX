#pragma once

class Camera
{
public:
  Camera();
  void Translate(float x, float y, float z);
  void ProcessKeyboard();
  DirectX::XMMATRIX LookAt();

private:
  float yaw, pitch;
  float speed, sensitivity;

  DirectX::XMFLOAT3 translate;
  DirectX::XMFLOAT3 right, forward;
};
