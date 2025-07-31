#pragma once

#include "DescriptorHeapListAllocator.h"
#include "HeapDescriptor.h"

namespace Renderer
{
inline constexpr D3D12_RANGE EMPTY_RANGE = {0, 0};

enum class MemoryUsage {
  GpuOnly = 0,
  CpuToGpu,
  GpuToCpu,
};

struct GpuBuffer {
  ID3D12Resource* Resource() const { return m_Resource.Get(); };

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(size_t offset = 0) const { return m_Resource->GetGPUVirtualAddress() + offset; }

  void AllocSrvDescriptor(DescriptorHeapListAllocator& allocator) { m_SrvDescriptor.Alloc(allocator); }

  void AllocSrvDescriptorWithGpuHandle(DescriptorHeapListAllocator& allocator)
  {
    m_SrvDescriptor.AllocWithGpuHandle(allocator);
  }

  void AllocUavDescriptor(DescriptorHeapListAllocator& allocator) { m_UavDescriptor.Alloc(allocator); }

  void AllocUavDescriptorWithGpuHandle(DescriptorHeapListAllocator& allocator)
  {
    m_UavDescriptor.AllocWithGpuHandle(allocator);
  }

  void AllocRtvDescriptor(DescriptorHeapListAllocator& allocator) { m_RtvDescriptor.Alloc(allocator); }

  UINT SrvDescriptorIndex() const { return m_SrvDescriptor.Index(); }

  UINT UavDescriptorIndex() const { return m_UavDescriptor.Index(); }

  D3D12_CPU_DESCRIPTOR_HANDLE SrvDescriptorHandle() const { return m_SrvDescriptor.CpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE SrvDescriptorGpuHandle() const { return m_SrvDescriptor.GpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE UavDescriptorHandle() const { return m_UavDescriptor.CpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE UavDescriptorGpuHandle() const { return m_UavDescriptor.GpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE RtvDescriptorHandle() const { return m_RtvDescriptor.CpuHandle(); }

  void Attach(ID3D12Resource* other) { m_Resource.Attach(other); }

  void CreateResource(D3D12MA::Allocator* allocator,
                      const D3D12MA::ALLOCATION_DESC* allocDesc,
                      const D3D12_RESOURCE_DESC* pResourceDesc,
                      D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON,
                      const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr)
  {
    m_CurrentState = InitialResourceState;
    CHECK_HR(allocator->CreateResource(allocDesc, pResourceDesc, InitialResourceState, pOptimizedClearValue,
                                       &m_Allocation, IID_PPV_ARGS(&m_Resource)));
  }

  void Map()
  {
    assert(!m_Mapped);

    CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, &m_Address));
    m_Mapped = true;
  }

  void Map(D3D12_RANGE* pReadRange, void** ppData)
  {
    assert(!m_Mapped);

    CHECK_HR(m_Resource->Map(0, pReadRange, ppData));
    m_Mapped = true;
  }

  void MapOpaque()
  {
    assert(!m_Mapped);

    CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, nullptr));
    m_Mapped = true;
  }

  void Unmap()
  {
    assert(m_Mapped);

    m_Resource->Unmap(0, nullptr);
    m_Mapped = false;
  }

  void Clear(size_t size)
  {
    assert(m_Mapped);

    ZeroMemory((BYTE*)m_Address, size);
  }

  void Copy(size_t offset, const void* data, size_t size)
  {
    assert(m_Mapped);

    memcpy((BYTE*)m_Address + offset, data, size);
  }

  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT DstSubresource = 0)
  {
    assert(m_Mapped);

    m_Resource->WriteToSubresource(DstSubresource, nullptr, data->pData, static_cast<UINT>(data->RowPitch),
                                   static_cast<UINT>(data->SlicePitch));
  }

  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource = 0)
  {
    assert(m_Mapped);

    for (UINT i = 0; i < numSubresources; ++i) {
      m_Resource->WriteToSubresource(firstSubresource + i, nullptr, data[i].pData, static_cast<UINT>(data[i].RowPitch),
                                     static_cast<UINT>(data[i].SlicePitch));
    }
  }

  D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
  {
    // TODO: implement state tracking assert(m_CurrentState == stateBefore);
    // maybe implement thin commandlist/queue wrapper?
    m_CurrentState = stateAfter;
    return CD3DX12_RESOURCE_BARRIER::Transition(m_Resource.Get(), stateBefore, stateAfter);
  }

  void SetName(std::wstring name)
  {
    std::wstring allocName = name + L" (Allocation)";

    m_ResourceName = name;
    m_Resource->SetName(name.c_str());
    m_Allocation->SetName(allocName.c_str());
  }

  void Reset()
  {
    if (m_Mapped) Unmap();

    m_Resource.Reset();
    if (m_Allocation) {
      m_Allocation->Release();
      m_Allocation = nullptr;
    }
  }

private:
  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  HeapDescriptor m_SrvDescriptor;
  HeapDescriptor m_UavDescriptor;
  HeapDescriptor m_RtvDescriptor;
  D3D12MA::Allocation* m_Allocation = nullptr;
  void* m_Address = nullptr;
  D3D12_RESOURCE_STATES m_CurrentState;
  std::wstring m_ResourceName;
  bool m_Mapped = false;
};

inline void AllocBuffer(GpuBuffer& buffer,
                 size_t bufSize,
                 std::wstring name,
                 D3D12MA::Allocator* allocator,
                 MemoryUsage memUsage = MemoryUsage::GpuOnly,
                 bool allowUnorderedAccess = false,
                 D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON)
{
  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};

  switch (memUsage) {
    case MemoryUsage::GpuOnly:
      allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
      break;
    case MemoryUsage::CpuToGpu:
      allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
      break;
    case MemoryUsage::GpuToCpu:
      allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
      break;
  }

  auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
  if (allowUnorderedAccess) {
    bufferDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  buffer.CreateResource(allocator, &allocDesc, &bufferDesc, InitialResourceState);
  buffer.SetName(name);

  if (memUsage == MemoryUsage::CpuToGpu) {
    buffer.Map();
  }
}

inline void CreateSrv(GpuBuffer& buffer,
               UINT numElements,
               UINT structureByteStride,
               ID3D12Device* device,
               DescriptorHeapListAllocator& allocator)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                          .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                          .Buffer = {
                                              .FirstElement = 0,
                                              .NumElements = numElements,
                                              .StructureByteStride = structureByteStride,
                                              .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                                          }};

  buffer.AllocSrvDescriptor(allocator);
  device->CreateShaderResourceView(buffer.Resource(), &srvDesc, buffer.SrvDescriptorHandle());
}

inline void CreateUav(GpuBuffer& buffer,
               size_t numElements,
               size_t structureByteStride,
               ID3D12Device* device,
               DescriptorHeapListAllocator& allocator,
               ID3D12Resource *pCounterResource = nullptr,
               UINT counterOffsetInBytes = 0)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                           .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                           .Buffer = {.FirstElement = 0,
                                                      .NumElements = static_cast<UINT>(numElements),
                                                      .StructureByteStride = static_cast<UINT>(structureByteStride),
                                                      .CounterOffsetInBytes = counterOffsetInBytes,
                                                      .Flags = D3D12_BUFFER_UAV_FLAG_NONE}};

  buffer.AllocUavDescriptor(allocator);
  device->CreateUnorderedAccessView(buffer.Resource(), pCounterResource, &uavDesc, buffer.UavDescriptorHandle());
}

}  // namespace Renderer
