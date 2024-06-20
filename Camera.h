#pragma once

class Camera
{
public:
  Camera();
  void Translate(float x, float y, float z);
  void Target(float x, float y, float z);
  void Orient(float p, float y);
  void ProcessKeyboard();
  DirectX::XMMATRIX LookAt();

  void DebugWindow();

private:
  float yaw, pitch;
  float speed, sensitivity;

  static const DirectX::XMVECTOR worldUp;

  DirectX::XMFLOAT3 translate;
  DirectX::XMFLOAT3 right, forward;
};
