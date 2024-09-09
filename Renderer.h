#pragma once

#include "D3D12MemAlloc.h"
#include "Mesh.h"
#include "Camera.h"
#include "Terrain.h"

#include <list>
#include <unordered_map>

namespace Renderer
{
void InitWindow(UINT width, UINT height, std::wstring name);
void InitAdapter(DXGIUsage*, IDXGIAdapter1*);
void Init();
void LoadAssets();
void Update(float);
void Render();
void Cleanup();
void PrintStatsString();

UINT GetWidth();
UINT GetHeight();
const WCHAR* GetTitle();

void SetSceneCamera(Camera* cam);

void AppendToScene(Model3D* model);
}  // namespace Renderer
