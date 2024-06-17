#pragma once

#include "RendererHelper.h"
#include "D3D12MemAlloc.h"
#include "Mesh.h"
#include "Camera.h"
#include "Terrain.h"

#include <list>
#include <unordered_map>

struct GPUSelection {
  UINT32 Index = UINT32_MAX;
  std::wstring Substring;
};

class DXGIUsage
{
public:
  void Init();
  IDXGIFactory4* GetDXGIFactory() const { return m_DXGIFactory.Get(); }
  void PrintAdapterList() const;
  // If failed, returns null pointer.
  ComPtr<IDXGIAdapter1> CreateAdapter(const GPUSelection&) const;

private:
  ComPtr<IDXGIFactory4> m_DXGIFactory;
};

namespace Renderer
{
void InitWindow(UINT width, UINT height, std::wstring name);
void InitAdapter(DXGIUsage*, GPUSelection);
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
