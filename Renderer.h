#pragma once

class Camera;
struct Model3D;

namespace Renderer
{
void InitWindow(UINT width, UINT height, std::wstring name);
void InitAdapter(DXGIUsage*, IDXGIAdapter1*);
void Init();
void LoadAssets();
void Update(float, float);
void Render();
void Cleanup();
void PrintStatsString();

UINT GetWidth();
UINT GetHeight();
const WCHAR* GetTitle();

void SetSceneCamera(Camera* cam);

void AppendToScene(Model3D* model);
}  // namespace Renderer
