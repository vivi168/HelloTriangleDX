#pragma once

#include "RendererHelper.h"
#include "D3D12MemAlloc.h"
#include "Mesh.h"
#include "Camera.h"
#include "Terrain.h"

#include <list>
#include <unordered_map>

using namespace DirectX;

static const size_t FRAME_BUFFER_COUNT = 3;

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

struct Geometry {
  ComPtr<ID3D12Resource> m_VertexBuffer;
  D3D12MA::Allocation* m_VertexBufferAllocation;
  D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;

  ComPtr<ID3D12Resource> m_IndexBuffer;
  D3D12MA::Allocation* m_IndexBufferAllocation;
  D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

  D3D12MA::Allocation* vBufferUploadHeapAllocation = nullptr;
  D3D12MA::Allocation* iBufferUploadHeapAllocation = nullptr;

  void Unload()
  {
    m_IndexBuffer.Reset();
    m_IndexBufferAllocation->Release();
    m_IndexBufferAllocation = nullptr;

    m_VertexBuffer.Reset();
    m_VertexBufferAllocation->Release();
    m_VertexBufferAllocation = nullptr;
  }
};

struct Texture {
  struct Header {
    uint16_t width;
    uint16_t height;
  } header;

  std::vector<uint8_t> pixels;
  std::string name;

  ComPtr<ID3D12Resource> m_Texture;
  D3D12MA::Allocation* m_TextureAllocation = nullptr;
  D3D12MA::Allocation* textureUploadAllocation = nullptr;

  unsigned int texIndex;

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    name = filename;

    fread(&header, sizeof(Header), 1, fp);
    pixels.resize(ImageSize());
    fread(pixels.data(), sizeof(uint8_t), ImageSize(), fp);
  }

  void Unload()
  {
    m_Texture.Reset();
    m_TextureAllocation->Release();
    m_TextureAllocation = nullptr;
  }

  DXGI_FORMAT Format() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }

  uint32_t BytesPerPixel() const { return 4; }

  uint32_t Width() const { return header.width; }

  uint32_t Height() const { return header.height; }

  uint64_t BytesPerRow() const { return Width() * BytesPerPixel(); }

  uint64_t ImageSize() const { return Height() * BytesPerRow(); }
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
