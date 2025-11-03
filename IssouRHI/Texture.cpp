#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{

static DXGI_FORMAT DXGIFormat(TextureFormat format)
{
  switch (format) {
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
  case TextureFormat::Undefined:
    assert(false); // FIXME std::unreachable(); C++23
  }
}

static D3D12_RESOURCE_FLAGS D3D12ResourceFlags(TextureUsage usage, TextureFormat format)
{
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

  // FIXME: derive from aspect/format ??
  // also can have more than one format for Depth/Stencil texture...
  // or better yet, simplify everything with a DepthStencil value in usage?
  if (format == TextureFormat::Depth32Float) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    return flags;
  }

  if (usage & TextureUsage::RenderAttachment) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }
  if (usage & TextureUsage::StorageBinding) {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  return flags;
}

static D3D12_RESOURCE_DESC D3D12ResourceDesc(TextureDesc& desc)
{
  switch (desc.dimension) {
    case TextureDimension::Texture1D:
      return CD3DX12_RESOURCE_DESC::Tex1D(
        DXGIFormat(desc.format),
        static_cast<UINT64>(desc.size.width),
        static_cast<UINT16>(desc.size.height),
        static_cast<UINT16>(desc.mipLevelCount),
        D3D12ResourceFlags(desc.usage, desc.format));
    case TextureDimension::Texture2D:
      return CD3DX12_RESOURCE_DESC::Tex2D(
        DXGIFormat(desc.format),
        static_cast<UINT64>(desc.size.width),
        static_cast<UINT16>(desc.size.height),
        static_cast<UINT16>(desc.size.depth),
        static_cast<UINT16>(desc.mipLevelCount),
        desc.sampleCount,
        0,
        D3D12ResourceFlags(desc.usage, desc.format));
    case TextureDimension::Texture3D:
      return CD3DX12_RESOURCE_DESC::Tex3D(
        DXGIFormat(desc.format),
        static_cast<UINT64>(desc.size.width),
        static_cast<UINT16>(desc.size.height),
        static_cast<UINT16>(desc.size.depth),
        static_cast<UINT16>(desc.mipLevelCount),
        D3D12ResourceFlags(desc.usage, desc.format));
  }
  return D3D12_RESOURCE_DESC{};
}

Texture::Texture(Device* device, TextureDesc& desc) : m_Device(device), m_Desc(desc)
{
  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};
  if (desc.usage & TextureUsage::CopyDst) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
  } else {
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  }

  D3D12_RESOURCE_DESC textureDesc = D3D12ResourceDesc(desc);

  CHECK_HR(device->GetAllocator()->CreateResource(&allocDesc, &textureDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                  &m_Allocation, IID_PPV_ARGS(&m_Resource)));
}

std::shared_ptr<TextureView> Texture::CreateView(TextureViewDesc& desc)
{
  // TODO
  return nullptr;
}

void Texture::Copy(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource)
{
  assert(m_Desc.usage & TextureUsage::CopyDst);

  if (!m_Mapped) {
    Map();
  }

  for (UINT i = 0; i < numSubresources; ++i) {
    m_Resource->WriteToSubresource(firstSubresource + i, nullptr, data[i].pData, static_cast<UINT>(data[i].RowPitch),
                                   static_cast<UINT>(data[i].SlicePitch));
  }
}

inline constexpr D3D12_RANGE EMPTY_RANGE = {0, 0};

void Texture::Map()
{
  assert(!m_Mapped);

  CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, nullptr));
  m_Mapped = true;
}

void Texture::Unmap()
{
  assert(m_Mapped);

  m_Resource->Unmap(0, nullptr);
  m_Mapped = false;
}

}  // namespace IssouRHI
