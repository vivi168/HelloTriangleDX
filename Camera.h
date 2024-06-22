#pragma once

class Camera
{
public:
  Camera();
  void Translate(float x, float y, float z);
  void Target(float x, float y, float z);
  void Orient(float p, float y);
  void ProcessKeyboard(float dt);
  DirectX::XMMATRIX LookAt();

  void DebugWindow();

private:
  float m_Yaw, m_Pitch;
  float m_Speed, m_Sensitivity;

  static const DirectX::XMVECTOR worldUp;

  DirectX::XMFLOAT3 m_Translate;
};
