#pragma once

#include "CommonD3D12.h"
#include "DescriptorHeapD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class TextureImpl : public Texture
{
public:
  TextureImpl(Device* device, const TextureDesc& desc);
  ~TextureImpl() override;

  void Create() override;

  void Write(std::span<TextureSubresource> subresources, uint32_t baseMipLevel = 0, uint32_t baseArrayLayer = 0) override;

  std::shared_ptr<TextureView> CreateView(const TextureViewDesc& desc) override;

public:
  void Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation = nullptr);

  D3D12_SHADER_RESOURCE_VIEW_DESC SrvDescriptor(const TextureViewDesc& desc) const;
  D3D12_UNORDERED_ACCESS_VIEW_DESC UavDescriptor(const TextureViewDesc& desc) const;
  // Internal use (OMSetRenderTargets, ClearRenderTargetView, etc)
  D3D12_RENDER_TARGET_VIEW_DESC RtvDescriptor(const TextureViewDesc& desc) const;
  D3D12_DEPTH_STENCIL_VIEW_DESC DsvDescriptor(const TextureViewDesc& desc) const;

  ID3D12Resource* Resource() const { return m_Resource.Get(); };

private:
  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  D3D12MA::Allocation* m_Allocation = nullptr;
};

inline TextureImpl* ToBackend(Texture* tex) { return static_cast<TextureImpl*>(tex); }

class TextureViewImpl : public TextureView
{
public:
   TextureViewImpl(Texture* tex, const TextureViewDesc& desc);
   ~TextureViewImpl() override;

   uint32_t DescriptorIndex(TextureAccess access) const override;
   uint64_t DescriptorHandle(TextureAccess access) const override;

public:
  void AllocDescriptors();

  DescriptorAllocation SrvDescriptorAlloc() const { return m_Srv; }

  DescriptorAllocation UavDescriptorAlloc() const { return m_Uav; }

  DescriptorAllocation RtvDescriptorAlloc() const { return m_Rtv; }

  DescriptorAllocation DsvDescriptorAlloc() const { return m_Dsv; }

private:
  DescriptorAllocation m_Srv;
  DescriptorAllocation m_Uav;
  DescriptorAllocation m_Rtv;
  DescriptorAllocation m_Dsv;
};

inline TextureViewImpl* ToBackend(TextureView* tv) { return static_cast<TextureViewImpl*>(tv); }
}  // namespace D3D12
}  // namespace IssouRHI
