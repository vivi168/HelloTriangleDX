#pragma once

class Camera
{
public:
    Camera();
    void Translate(float x, float y, float z);
    void ProcessInput();
private:
    void Update();

    float yaw, pitch;
    DirectX::XMFLOAT3 translate;
    DirectX::XMFLOAT4X4 viewMatrix;

    DirectX::XMFLOAT3 front, up, right, forward;
    DirectX::XMFLOAT3 worldUp;

    float speed, sensitivity;
};