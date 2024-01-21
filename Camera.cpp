#include "stdafx.h"
#include "Camera.h"
#include "Input.h"

using namespace DirectX;

Camera::Camera()
{
    yaw = -XM_PIDIV2;
    pitch = 0;
    speed = 5.0f;
    sensitivity = 0.05f;

    worldUp = { 0, 1, 0 };
    translate = { 0, 0, 0 };
}

void Camera::Translate(float x, float y, float z)
{
    translate = { x, y, z };
}

void Camera::ProcessInput()
{

}
