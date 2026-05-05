#include "TextureD3D12.h"

#include "DeviceD3D12.h"
#include "UtilsD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
TextureImpl::TextureImpl(Device* device, const TextureDesc& desc) : Texture(device, desc) {}

TextureImpl::~TextureImpl()
{
  if (m_Allocation) {
    m_Allocation->Release();
    m_Allocation = nullptr;
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

static D3D12_RESOURCE_DESC1 D3D12ResourceDesc(const TextureDesc& desc)
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

void TextureImpl::Create()
{
  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};
  if (m_Desc.usage & TextureUsage::CopyDst) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
  } else {
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  }

  D3D12_RESOURCE_DESC1 textureDesc = D3D12ResourceDesc(m_Desc);

  D3D12_CLEAR_VALUE zero{};
  zero.Format = textureDesc.Format;

  // TODO: how to allow for another clear value / any value for .clearValue of ColorAttachment?
  D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr;
  if (textureDesc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
    pOptimizedClearValue = &zero;
  }

  D3D12_BARRIER_LAYOUT initialLayout = D3D12_BARRIER_LAYOUT_COMMON;
  if (textureDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) {
    zero.DepthStencil.Depth = 1.0f;
    zero.DepthStencil.Stencil = 0;
    initialLayout = D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
  }

  ID3D12Resource* resource;
  D3D12MA::Allocation* allocation;
  CHECK_HR(ToBackend(m_Device)->GetAllocator()->CreateResource3(&allocDesc, &textureDesc, initialLayout, pOptimizedClearValue, 0, nullptr, &allocation, IID_PPV_ARGS(&resource)));
  m_Resource.Attach(resource);
  m_Allocation = allocation;

  m_Resource->SetName(StringToWstring(m_Desc.label).c_str());
}

void TextureImpl::Write(std::span<TextureSubresource> subresources, uint32_t baseMipLevel, uint32_t baseArrayLayer)
{
  assert(Usage() & TextureUsage::CopyDst);
  assert(baseMipLevel < m_Desc.mipLevelCount);

  constexpr D3D12_RANGE EMPTY_RANGE = {0, 0};
  CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, nullptr));

  UINT mipCount = m_Desc.mipLevelCount;
  UINT arrayLayerCount = m_Desc.size.depth;

  for (UINT i = 0; i < subresources.size(); ++i) {
    UINT mipOffset = i % (mipCount - baseMipLevel);
    UINT arrayOffset = i / (mipCount - baseMipLevel);

    UINT mipLevel = baseMipLevel + mipOffset;
    UINT arrayLayer = baseArrayLayer + arrayOffset;

    assert(mipLevel < mipCount);
    assert(arrayLayer < arrayLayerCount);

    UINT dstSubresource = D3D12CalcSubresource(mipLevel, arrayLayer, 0, mipCount, arrayLayerCount);

    const TextureSubresource& src = subresources[i];

    CHECK_HR(m_Resource->WriteToSubresource(dstSubresource, nullptr, src.data, static_cast<UINT>(src.rowPitch), static_cast<UINT>(src.slicePitch)));
  }

  m_Resource->Unmap(0, nullptr);
}

std::shared_ptr<TextureView> TextureImpl::CreateView(const TextureViewDesc& desc)
{
  if (auto it = m_Views.find(desc); it != m_Views.end()) {
    return it->second;
  }

  auto view = std::make_shared<TextureViewImpl>(this, desc);
  view->AllocDescriptors();

  if (Usage() & TextureUsage::TextureBinding) {
    auto srvDesc = SrvDescriptor(desc);
    ToBackend(m_Device)->GetNativeDevice()->CreateShaderResourceView(m_Resource.Get(), &srvDesc, view->SrvDescriptorAlloc().cpuHandle);
  }

  if (Usage() & TextureUsage::StorageBinding) {
    auto uavDesc = UavDescriptor(desc);
    ToBackend(m_Device)->GetNativeDevice()->CreateUnorderedAccessView(m_Resource.Get(), nullptr, &uavDesc, view->UavDescriptorAlloc().cpuHandle);
  }

  if (Usage() & TextureUsage::RenderAttachment) {
    if (IsDepthStencil(Format())) {
      auto dsvDesc = DsvDescriptor(desc);
      ToBackend(m_Device)->GetNativeDevice()->CreateDepthStencilView(m_Resource.Get(), &dsvDesc, view->DsvDescriptorAlloc().cpuHandle);
    } else {
      auto rtvDesc = RtvDescriptor(desc);
      ToBackend(m_Device)->GetNativeDevice()->CreateRenderTargetView(m_Resource.Get(), &rtvDesc, view->RtvDescriptorAlloc().cpuHandle);
    }
  }

  m_Views[desc] = view;

  return view;
}

void TextureImpl::Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation)
{
  m_Resource.Attach(other);

  if (allocation) {
    m_Allocation = allocation;
  }
}

D3D12_SHADER_RESOURCE_VIEW_DESC TextureImpl::SrvDescriptor(const TextureViewDesc& desc) const
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

D3D12_UNORDERED_ACCESS_VIEW_DESC TextureImpl::UavDescriptor(const TextureViewDesc& desc) const
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

D3D12_RENDER_TARGET_VIEW_DESC TextureImpl::RtvDescriptor(const TextureViewDesc& desc) const
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

D3D12_DEPTH_STENCIL_VIEW_DESC TextureImpl::DsvDescriptor(const TextureViewDesc& desc) const
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

void TextureViewImpl::AllocDescriptors()
{
  if (m_Texture->Usage() & TextureUsage::TextureBinding) {
    m_Srv = ToBackend(m_Texture->GetDevice())->AllocCbvSrvUavDescriptor();
  }

  if (m_Texture->Usage() & TextureUsage::StorageBinding) {
    m_Uav = ToBackend(m_Texture->GetDevice())->AllocCbvSrvUavDescriptor();
  }

  if (m_Texture->Usage() & TextureUsage::RenderAttachment) {
    if (IsDepthStencil(m_Texture->Format())) {
      m_Dsv = ToBackend(m_Texture->GetDevice())->AllocDsvDescriptor();
    } else {
      m_Rtv = ToBackend(m_Texture->GetDevice())->AllocRtvDescriptor();
    }
  }
}

TextureViewImpl::TextureViewImpl(Texture* tex, const TextureViewDesc& desc) : TextureView(tex, desc) {}

TextureViewImpl::~TextureViewImpl()
{
  ToBackend(m_Texture->GetDevice())->FreeSrvUavDescriptor(m_Srv);
  ToBackend(m_Texture->GetDevice())->FreeSrvUavDescriptor(m_Uav);
  ToBackend(m_Texture->GetDevice())->FreeRtvDescriptor(m_Rtv);
  ToBackend(m_Texture->GetDevice())->FreeDsvDescriptor(m_Dsv);
}

uint32_t TextureViewImpl::DescriptorIndex(TextureAccess access) const
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

uint64_t TextureViewImpl::DescriptorHandle(TextureAccess access) const
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
}  // namespace D3D12
}  // namespace IssouRHI
