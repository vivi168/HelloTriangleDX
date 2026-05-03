#pragma once

#include "IssouRHI.h"

class Camera;
struct Model3D;

namespace Renderer
{
inline constexpr size_t FRAME_BUFFER_COUNT = 3;

void InitWindow(UINT width, UINT height, std::wstring name);
void Init(std::unique_ptr<IssouRHI::Device> device);
void LoadAssets();
void Render(float time);
void Cleanup();
void PrintStatsString();

UINT GetWidth();
UINT GetHeight();
const WCHAR* GetTitle();

void SetSceneCamera(Camera* cam);

void AppendToScene(Model3D* model);

UINT CreateMaterial(std::filesystem::path baseDir, std::wstring filename);
}  // namespace Renderer
