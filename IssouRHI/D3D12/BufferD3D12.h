#pragma once

#include "CommonD3D12.h"
#include "DescriptorHeapD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class BufferImpl : public Buffer
{
public:
  BufferImpl(Device* device, const BufferDesc& desc);
  ~BufferImpl() override;

  void Create() override;

  uint32_t DescriptorIndex(const BufferViewDesc& desc) override;

  uint64_t GpuAddress() const override { return m_Resource->GetGPUVirtualAddress(); }

  void Write(BufferRange range, const void* data) override;
  void Clear(BufferRange range) override;
  void Read(BufferRange range, void* outData) override;

public:
  ID3D12Resource* Resource() const { return m_Resource.Get(); };

private:
  void Map();
  void Unmap();

  DescriptorAllocation CbvDescriptorAlloc(BufferRange range);
  DescriptorAllocation SrvDescriptorAlloc(BufferRange range, UINT byteStride);
  DescriptorAllocation UavDescriptorAlloc(BufferRange range, UINT byteStride, Buffer* counter, UINT64 counterOffsetInBytes);

  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  D3D12MA::Allocation* m_Allocation = nullptr;
  void* m_Address = nullptr;

  struct ViewKey {
    BufferRange range;
    UINT byteStride = 0;
    Buffer* counter = nullptr;
    UINT64 counterOffsetInBytes = 0;

    bool operator==(const ViewKey& other) const
    {
      return range == other.range && byteStride == other.byteStride && counter == other.counter &&
             counterOffsetInBytes == other.counterOffsetInBytes;
    }

    struct Hasher {
      size_t operator()(const ViewKey& vk) const
      {
        size_t hash = 0;

        HashCombine(hash, vk.range.offset);
        HashCombine(hash, vk.range.size);
        HashCombine(hash, vk.byteStride);
        HashCombine(hash, reinterpret_cast<std::uintptr_t>(vk.counter));
        HashCombine(hash, vk.counterOffsetInBytes);

        return hash;
      }
    };
  };

  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Cbvs{};
  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Srvs{};
  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Uavs{};
};

inline BufferImpl* ToBackend(Buffer* buffer) { return static_cast<BufferImpl*>(buffer); }
}  // namespace D3D12
}  // namespace IssouRHI
