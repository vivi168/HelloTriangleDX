#pragma once

#include "CommonD3D12.h"

#include <string>
#include <vector>

namespace IssouRHI
{
namespace D3D12
{
std::wstring StringToWstring(std::string_view s);
void ReportLiveObjects(); // TODO: move this to device static member + D3D12 only
void PrintAdapterList();

inline DXGI_FORMAT DXGIFormat(TextureFormat format)
{
  switch (format) {
    case TextureFormat::Undefined:
      return DXGI_FORMAT_UNKNOWN;
    case TextureFormat::BC5Unorm:
      return DXGI_FORMAT_BC5_UNORM;
    case TextureFormat::BC7Unorm:
      return DXGI_FORMAT_BC7_UNORM;
    case TextureFormat::Depth32Float:
      return DXGI_FORMAT_D32_FLOAT;
    case TextureFormat::R8Unorm:
      return DXGI_FORMAT_R8_UNORM;
    case TextureFormat::RG8Unorm:
      return DXGI_FORMAT_R8G8_UNORM;
    case TextureFormat::R32Uint:
      return DXGI_FORMAT_R32_UINT;
    case TextureFormat::RGBA8Unorm:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::RGB10A2Unorm:
      return DXGI_FORMAT_R10G10B10A2_UNORM;
    case TextureFormat::RGBA32Float:
      return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default:
      std::unreachable(); // FIXME
  }
}

inline DXGI_FORMAT DXGIFormat(VertexFormat format)
{
  switch (format) {
    case VertexFormat::Undefined:
      return DXGI_FORMAT_UNKNOWN;
    case VertexFormat::Float32x3:
      return DXGI_FORMAT_R32G32B32_FLOAT;
    default:
      std::unreachable();
  }
}

inline DXGI_FORMAT DXGIFormat(IndexFormat format)
{
  switch (format) {
    case IndexFormat::Undefined:
      return DXGI_FORMAT_UNKNOWN;
    case IndexFormat::Uint16:
      return DXGI_FORMAT_R16_UINT;
    case IndexFormat::Uint32:
      return DXGI_FORMAT_R32_UINT;
    default:
      std::unreachable();
  }
}

std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> D3D12RaytracingGeometryDescs(std::span<BottomLevelGeometryDesc> geometries);

}
}
