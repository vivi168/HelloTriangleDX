#pragma once

class Camera;
struct Model3D;

namespace Renderer
{
void InitWindow(UINT width, UINT height, std::wstring name);
void InitAdapter(IDXGIFactory4*, IDXGIAdapter1*);
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

UINT CreateMaterial(std::filesystem::path baseDir, std::wstring filename);
}  // namespace Renderer
