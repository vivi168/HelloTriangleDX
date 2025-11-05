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
    return DXGI_FORMAT_UNKNOWN;
  }
}

static bool IsDepthStencil(TextureFormat format)
{
  switch (format) {
    case TextureFormat::Depth32Float:
      return true;
    default:
      return false;
  }
}

static D3D12_RESOURCE_FLAGS D3D12ResourceFlags(TextureUsage usage, TextureFormat format)
{
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;

  if (IsDepthStencil(format)) {
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

static uint32_t PlaneSlice(TextureAspect aspect)
{
  switch (aspect) {
    case TextureAspect::All:
    case TextureAspect::DepthOnly:
      return 0;
    case TextureAspect::StencilOnly:
      // TODO
      return 1;
  }
}

D3D12_RESOURCE_DESC Texture::D3D12ResourceDesc(TextureDesc& desc)
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

Texture::Texture(Device* device, TextureDesc& desc) : m_Device(device), m_Desc(desc) {}

Texture::~Texture()
{
  if (m_Mapped) Unmap();

  if (m_Allocation) {
    m_Allocation->Release();
    m_Allocation = nullptr;
  }
}

std::shared_ptr<TextureView> Texture::CreateView(TextureViewDesc& desc)
{
  auto view = std::make_shared<TextureView>(this, desc);
  // call device->CreateUnorderedAccessView etc from here?
  return view;
}

void Texture::Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation)
{
  m_Resource.Attach(other);

  if (allocation) {
    m_Allocation = allocation;
  }
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

D3D12_SHADER_RESOURCE_VIEW_DESC Texture::SrvDescriptor(TextureViewDesc& desc) const
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = DXGIFormat(desc.format);
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  switch (desc.dimension) {
    case TextureViewDimension::Texture1D:
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
      // TODO
      break;
    case TextureViewDimension::Texture2D:
      if (m_Desc.sampleCount > 1) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        // TODO
      } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = desc.range.baseMipLevel;
        srvDesc.Texture2D.MipLevels = desc.range.mipLevelCount;
        srvDesc.Texture2D.PlaneSlice = PlaneSlice(desc.aspect);
      }
      break;
    case TextureViewDimension::Texture2DAry:
      if (m_Desc.sampleCount > 1) {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        // TODO
      } else {
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        // TODO
      }
      break;
    case TextureViewDimension::TextureCube:
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      // TODO
      break;
    case TextureViewDimension::TextureCubeAry:
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
      // TODO
      break;
    case TextureViewDimension::Texture3D:
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      // TODO
      break;
  }

  return srvDesc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC Texture::UavDescriptor(TextureViewDesc& desc) const
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
  uavDesc.Format = DXGIFormat(desc.format);

  switch (desc.dimension) {
    case TextureViewDimension::Texture1D:
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
      uavDesc.Texture1D.MipSlice = desc.range.baseMipLevel;
      break;
    case TextureViewDimension::Texture2D:
    case TextureViewDimension::Texture2DAry:
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
      uavDesc.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
      uavDesc.Texture2DArray.ArraySize = desc.range.arrayLayerCount;
      uavDesc.Texture2DArray.MipSlice = desc.range.baseMipLevel;
      uavDesc.Texture2DArray.PlaneSlice = 0;
      break;
    case TextureViewDimension::TextureCube:
    case TextureViewDimension::TextureCubeAry:
      assert(false);
      // std::unreachable();
      break;
    case TextureViewDimension::Texture3D:
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
      uavDesc.Texture3D.MipSlice = desc.range.baseMipLevel;
      uavDesc.Texture3D.FirstWSlice = 0;
      uavDesc.Texture3D.WSize = UINT_MAX;
      break;
  }

  return uavDesc;
}

D3D12_RENDER_TARGET_VIEW_DESC Texture::RtvDescriptor(TextureViewDesc& desc) const
{
  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
  rtvDesc.Format = DXGIFormat(desc.format);

  switch (desc.dimension) {
    case TextureViewDimension::Texture1D:
      // TODO
      break;
    case TextureViewDimension::Texture2D:
      // TODO
      break;
    case TextureViewDimension::Texture2DAry:
      // TODO
      break;
    case TextureViewDimension::TextureCube:
      // TODO
      break;
    case TextureViewDimension::TextureCubeAry:
      // TODO
      break;
    case TextureViewDimension::Texture3D:
      // TODO
      break;
  }

  return rtvDesc;
}

D3D12_DEPTH_STENCIL_VIEW_DESC Texture::DsvDescriptor(TextureViewDesc& desc) const
{
  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
  dsvDesc.Format = DXGIFormat(desc.format);

  switch (desc.dimension) {
    case TextureViewDimension::Texture1D:
      // TODO
      break;
    case TextureViewDimension::Texture2D:
      // TODO
      break;
    case TextureViewDimension::Texture2DAry:
      // TODO
      break;
    case TextureViewDimension::TextureCube:
      // TODO
      break;
    case TextureViewDimension::TextureCubeAry:
      // TODO
      break;
    case TextureViewDimension::Texture3D:
      // TODO
      break;
  }

  return dsvDesc;
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

TextureView::TextureView(Texture* tex, TextureViewDesc& desc) : m_Texture(tex), m_Desc(desc) {}

TextureView::~TextureView()
{
  // TODO: Free Allocations etc
}

}  // namespace IssouRHI
