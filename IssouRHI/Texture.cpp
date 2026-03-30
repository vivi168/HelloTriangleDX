#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{

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

D3D12_RESOURCE_DESC1 Texture::D3D12ResourceDesc(const TextureDesc& desc)
{
  switch (desc.dimension) {
    case TextureDimension::Texture1D:
      return CD3DX12_RESOURCE_DESC1::Tex1D(
        DXGIFormat(desc.format),
        static_cast<UINT64>(desc.size.width),
        static_cast<UINT16>(desc.size.height),
        static_cast<UINT16>(desc.mipLevelCount),
        D3D12ResourceFlags(desc.usage, desc.format));
    case TextureDimension::Texture2D:
      return CD3DX12_RESOURCE_DESC1::Tex2D(
        DXGIFormat(desc.format),
        static_cast<UINT64>(desc.size.width),
        static_cast<UINT16>(desc.size.height),
        static_cast<UINT16>(desc.size.depth),
        static_cast<UINT16>(desc.mipLevelCount),
        desc.sampleCount,
        0,
        D3D12ResourceFlags(desc.usage, desc.format));
    case TextureDimension::Texture3D:
      return CD3DX12_RESOURCE_DESC1::Tex3D(
        DXGIFormat(desc.format),
        static_cast<UINT64>(desc.size.width),
        static_cast<UINT16>(desc.size.height),
        static_cast<UINT16>(desc.size.depth),
        static_cast<UINT16>(desc.mipLevelCount),
        D3D12ResourceFlags(desc.usage, desc.format));
  }
  return D3D12_RESOURCE_DESC1{};
}

Texture::Texture(Device* device, const TextureDesc& desc) : m_Device(device), m_Desc(desc)
{
  m_CurrentStageAccessLayout.stage = D3D12_BARRIER_SYNC_NONE;
  m_CurrentStageAccessLayout.access = D3D12_BARRIER_ACCESS_NO_ACCESS;
  // TODO: sync/reuse somehow with CreateTexture#initialLayout
  m_CurrentStageAccessLayout.layout = D3D12_BARRIER_LAYOUT_COMMON;
}

Texture::~Texture()
{
  if (m_Allocation) {
    m_Allocation->Release();
    m_Allocation = nullptr;
  }
}

static TextureViewDimension ViewDimension(TextureDimension dim)
{
  switch (dim) {
    case TextureDimension::Texture1D:
      return TextureViewDimension::Texture1D;
    case TextureDimension::Texture2D:
      return TextureViewDimension::Texture2D;
    case TextureDimension::Texture3D:
      return TextureViewDimension::Texture3D;
  }
}

std::shared_ptr<TextureView> Texture::CreateView()
{
  TextureViewDesc desc{};
  desc.format = Format();
  desc.dimension = ViewDimension(m_Desc.dimension);
  desc.range = {
    .baseMipLevel = 0,
    .mipLevelCount = m_Desc.mipLevelCount,
    .baseArrayLayer = 0,
    .arrayLayerCount = m_Desc.size.depth,
  };

  return CreateView(desc);
}

std::shared_ptr<TextureView> Texture::CreateView(const TextureViewDesc& desc)
{
  if (auto it = m_Views.find(desc); it != m_Views.end()) {
    return it->second;
  }

  auto view = std::make_shared<TextureView>(this, desc);
  auto device = m_Device->GetNativeDevice();

  if (Usage() & TextureUsage::TextureBinding) {
    auto srvDesc = SrvDescriptor(desc);
    device->CreateShaderResourceView(m_Resource.Get(), &srvDesc, view->SrvDescriptorAlloc().cpuHandle);
  }

  if (Usage() & TextureUsage::StorageBinding) {
    auto uavDesc = UavDescriptor(desc);
    device->CreateUnorderedAccessView(m_Resource.Get(), nullptr, &uavDesc, view->UavDescriptorAlloc().cpuHandle);
  }

  if (Usage() & TextureUsage::RenderAttachment) {
    if (IsDepthStencil(Format())) {
      auto dsvDesc = DsvDescriptor(desc);
      device->CreateDepthStencilView(m_Resource.Get(), &dsvDesc, view->DsvDescriptorAlloc().cpuHandle);
    } else {
      auto rtvDesc = RtvDescriptor(desc);
      device->CreateRenderTargetView(m_Resource.Get(), &rtvDesc, view->RtvDescriptorAlloc().cpuHandle);
    }
  }

  m_Views[desc] = view;

  return view;
}

void Texture::Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation)
{
  m_Resource.Attach(other);

  if (allocation) {
    m_Allocation = allocation;
  }
}

void Texture::WriteToSubresource(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource)
{
  assert(Usage() & TextureUsage::CopyDst);

  constexpr D3D12_RANGE EMPTY_RANGE = {0, 0};
  CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, nullptr));

  for (UINT i = 0; i < numSubresources; ++i) {
    m_Resource->WriteToSubresource(firstSubresource + i, nullptr, data[i].pData, static_cast<UINT>(data[i].RowPitch),
                                   static_cast<UINT>(data[i].SlicePitch));
  }

  m_Resource->Unmap(0, nullptr);
}

std::optional<D3D12_TEXTURE_BARRIER> Texture::Transition(StageAccessLayout to)
{
  // mutex? no because we should NOT keep track of state from this class...
  bool accessLayoutChanged = m_CurrentStageAccessLayout.access != to.access || m_CurrentStageAccessLayout.layout != to.layout;
  bool storageBarrier = m_CurrentStageAccessLayout.access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS && to.access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

  if (!accessLayoutChanged && !storageBarrier)
    return std::nullopt;

  auto barrier = CD3DX12_TEXTURE_BARRIER(m_CurrentStageAccessLayout.stage,
                                         to.stage,
                                         m_CurrentStageAccessLayout.access,
                                         to.access,
                                         m_CurrentStageAccessLayout.layout,
                                         to.layout,
                                         Resource(),
                                         CD3DX12_BARRIER_SUBRESOURCE_RANGE(0xffffffff),  // TODO
                                         D3D12_TEXTURE_BARRIER_FLAG_NONE);
  // TODO: should probably update AFTER cmdList->Barrier has been called with the above barrier...
  // leave that responsability to the future command list class ?
  m_CurrentStageAccessLayout = to;
  return barrier;
}

D3D12_SHADER_RESOURCE_VIEW_DESC Texture::SrvDescriptor(const TextureViewDesc& desc) const
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
      if (IsMultiSampled()) {
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
      if (IsMultiSampled()) {
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

D3D12_UNORDERED_ACCESS_VIEW_DESC Texture::UavDescriptor(const TextureViewDesc& desc) const
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
      std::unreachable();
    case TextureViewDimension::Texture3D:
      uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
      uavDesc.Texture3D.MipSlice = desc.range.baseMipLevel;
      uavDesc.Texture3D.FirstWSlice = 0;
      uavDesc.Texture3D.WSize = UINT_MAX;
      break;
  }

  return uavDesc;
}

D3D12_RENDER_TARGET_VIEW_DESC Texture::RtvDescriptor(const TextureViewDesc& desc) const
{
  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
  rtvDesc.Format = DXGIFormat(desc.format);

  if (IsMultiSampled()) {
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
    // TODO
  } else {
    switch (m_Desc.dimension) {
      case TextureDimension::Texture1D:
        std::unreachable();
      case TextureDimension::Texture2D:
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rtvDesc.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
        rtvDesc.Texture2DArray.ArraySize = desc.range.arrayLayerCount;
        rtvDesc.Texture2DArray.MipSlice = desc.range.baseMipLevel;
        rtvDesc.Texture2DArray.PlaneSlice = 0;
        break;
      case TextureDimension::Texture3D:
        // TODO
        break;
    }
  }

  return rtvDesc;
}

D3D12_DEPTH_STENCIL_VIEW_DESC Texture::DsvDescriptor(const TextureViewDesc& desc) const
{
  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
  dsvDesc.Format = DXGIFormat(desc.format);

  dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
  if (desc.aspect == TextureAspect::DepthOnly) {
    dsvDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
  } else if (desc.aspect == TextureAspect::StencilOnly) {
    dsvDesc.Flags |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
  }

  if (IsMultiSampled()) {
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
    // TODO
  } else {
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.FirstArraySlice = desc.range.baseArrayLayer;
    dsvDesc.Texture2DArray.ArraySize = desc.range.arrayLayerCount;
    dsvDesc.Texture2DArray.MipSlice = desc.range.baseMipLevel;
  }

  return dsvDesc;
}

TextureView::TextureView(Texture* tex, const TextureViewDesc& desc) : m_Texture(tex), m_Desc(desc)
{
  if (m_Texture->Usage() & TextureUsage::TextureBinding) {
    m_Srv = m_Texture->GetDevice()->AllocCbvSrvUavDescriptor();
  }

  if (m_Texture->Usage() & TextureUsage::StorageBinding) {
    m_Uav = m_Texture->GetDevice()->AllocCbvSrvUavDescriptor();
  }

  if (m_Texture->Usage() & TextureUsage::RenderAttachment) {
    if (IsDepthStencil(m_Texture->Format())) {
      m_Dsv = m_Texture->GetDevice()->AllocDsvDescriptor();
    } else {
      m_Rtv = m_Texture->GetDevice()->AllocRtvDescriptor();
    }
  }
}

TextureView::~TextureView()
{
  m_Texture->GetDevice()->FreeSrvUavDescriptor(m_Srv);
  m_Texture->GetDevice()->FreeSrvUavDescriptor(m_Uav);
  m_Texture->GetDevice()->FreeRtvDescriptor(m_Rtv);
  m_Texture->GetDevice()->FreeDsvDescriptor(m_Dsv);
}

uint32_t TextureView::DescriptorIndex(TextureAccess access) const
{
  switch (access) {
  case TextureAccess::Read:
    return m_Srv.index;
  case TextureAccess::ReadWrite:
    return m_Uav.index;
  default:
    std::unreachable();
  }
}

uint64_t TextureView::DescriptorHandle(TextureAccess access) const
{
  switch (access) {
  case TextureAccess::Read:
    return m_Srv.gpuHandle.ptr;
  case TextureAccess::ReadWrite:
    return m_Uav.gpuHandle.ptr;
  default:
    std::unreachable();
  }
}

}  // namespace IssouRHI
