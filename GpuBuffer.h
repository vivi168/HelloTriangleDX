#pragma once

#include "DescriptorHeapListAllocator.h"
#include "HeapDescriptor.h"

namespace Renderer
{
inline constexpr D3D12_RANGE EMPTY_RANGE = {0, 0};

enum class HeapType {
  Default = 0,
  Upload,
  Readback,
};

struct GpuResource {
  ID3D12Resource* Resource() const { return m_Resource.Get(); };

  GpuResource& CreateResource(D3D12MA::Allocator* allocator,
                      const D3D12MA::ALLOCATION_DESC* allocDesc,
                      const D3D12_RESOURCE_DESC* pResourceDesc,
                      D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON,
                      const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr)
  {
    m_CurrentState = InitialResourceState;
    CHECK_HR(allocator->CreateResource(allocDesc, pResourceDesc, InitialResourceState, pOptimizedClearValue,
                                       &m_Allocation, IID_PPV_ARGS(&m_Resource)));

    return *this;
  }

  D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
  {
    // TODO: implement state tracking assert(m_CurrentState == stateBefore);
    // maybe implement thin commandlist/queue wrapper?
    m_CurrentState = stateAfter;
    return CD3DX12_RESOURCE_BARRIER::Transition(m_Resource.Get(), stateBefore, stateAfter);
  }

  virtual void AllocSrvDescriptor(DescriptorHeapListAllocator& allocator) { m_SrvDescriptor.Alloc(allocator); }

  virtual void AllocUavDescriptor(DescriptorHeapListAllocator& allocator) { m_UavDescriptor.Alloc(allocator); }

  UINT SrvDescriptorIndex() const { return m_SrvDescriptor.Index(); }

  UINT UavDescriptorIndex() const { return m_UavDescriptor.Index(); }

  GpuResource& SetName(std::wstring name)
  {
    std::wstring allocName = name + L" (Allocation)";

    m_ResourceName = name;
    m_Resource->SetName(name.c_str());
    m_Allocation->SetName(allocName.c_str());

    return *this;
  }

  virtual GpuResource& Map() = 0;

  void Unmap()
  {
    assert(m_Mapped);

    m_Resource->Unmap(0, nullptr);
    m_Mapped = false;
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

protected:
  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  HeapDescriptor m_SrvDescriptor;
  HeapDescriptor m_UavDescriptor;
  D3D12MA::Allocation* m_Allocation = nullptr;
  D3D12_RESOURCE_STATES m_CurrentState;
  std::wstring m_ResourceName;
  bool m_Mapped = false;
};

struct GpuBuffer : GpuResource {
  GpuBuffer& Alloc(size_t bufSize,
                   std::wstring name,
                   D3D12MA::Allocator* allocator,
                   HeapType memUsage = HeapType::Default,
                   bool allowUnorderedAccess = false,
                   D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON)
  {
    D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};

    switch (memUsage) {
      case HeapType::Default:
        allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
        break;
      case HeapType::Upload:
        allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
        break;
      case HeapType::Readback:
        allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
        break;
    }

    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
    if (allowUnorderedAccess) {
      bufferDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    CreateResource(allocator, &allocDesc, &bufferDesc, InitialResourceState).SetName(name);

    if (memUsage == HeapType::Upload) {
      Map();
    }

    m_Size = bufSize;

    return *this;
  }

  GpuBuffer& CreateSrv(UINT numElements,
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

    AllocSrvDescriptor(allocator);
    device->CreateShaderResourceView(Resource(), &srvDesc, SrvDescriptorHandle());

    return *this;
  }

  GpuBuffer& CreateAccelStructSrv(ID3D12Device* device, DescriptorHeapListAllocator& allocator)
  {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE,
                                            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                            .RaytracingAccelerationStructure = {.Location = GpuAddress()}};

    AllocSrvDescriptor(allocator);
    device->CreateShaderResourceView(nullptr, &srvDesc, SrvDescriptorHandle());

    return *this;
  }

  GpuBuffer& CreateUav(size_t numElements,
                       size_t structureByteStride,
                       ID3D12Device* device,
                       DescriptorHeapListAllocator& allocator,
                       ID3D12Resource* pCounterResource = nullptr,
                       UINT counterOffsetInBytes = 0)
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                             .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                             .Buffer = {.FirstElement = 0,
                                                        .NumElements = static_cast<UINT>(numElements),
                                                        .StructureByteStride = static_cast<UINT>(structureByteStride),
                                                        .CounterOffsetInBytes = counterOffsetInBytes,
                                                        .Flags = D3D12_BUFFER_UAV_FLAG_NONE}};

    AllocUavDescriptor(allocator);
    device->CreateUnorderedAccessView(Resource(), pCounterResource, &uavDesc, UavDescriptorHandle());

    return *this;
  }

  GpuBuffer& Map() override
  {
    assert(!m_Mapped);

    CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, &m_Address));
    m_Mapped = true;

    return *this;
  }

  GpuBuffer& Map(D3D12_RANGE* pReadRange, void** ppData)
  {
    assert(!m_Mapped);

    CHECK_HR(m_Resource->Map(0, pReadRange, ppData));
    m_Mapped = true;

    return *this;
  }

  GpuBuffer& Clear(size_t size)
  {
    assert(m_Mapped);

    ZeroMemory((BYTE*)m_Address, size);

    return *this;
  }

  GpuBuffer& Copy(size_t offset, const void* data, size_t size)
  {
    assert(m_Mapped);

    memcpy((BYTE*)m_Address + offset, data, size);

    return *this;
  }

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(size_t offset = 0) const { return m_Resource->GetGPUVirtualAddress() + offset; }

  size_t Size() const { return m_Size; }

private:
  D3D12_CPU_DESCRIPTOR_HANDLE SrvDescriptorHandle() const { return m_SrvDescriptor.CpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE UavDescriptorHandle() const { return m_UavDescriptor.CpuHandle(); }

  void* m_Address = nullptr;
  size_t m_Size;
};

// TODO: use this to create textures
struct TextureDesc {
  UINT width;
  UINT height;
  DXGI_FORMAT format;
  UINT mipLevels;
  UINT arraySize;
  bool allowUnorderedAccess;
  bool allowRenderTarget;
  bool allowDepthStencil;
};

struct Texture : GpuResource {
  void AllocSrvDescriptor(DescriptorHeapListAllocator& allocator) override
  {
    m_SrvDescriptor.AllocWithGpuHandle(allocator);
  }

  void AllocUavDescriptor(DescriptorHeapListAllocator& allocator) override
  {
    m_UavDescriptor.AllocWithGpuHandle(allocator);
  }

  void AllocRtvDescriptor(DescriptorHeapListAllocator& allocator) { m_RtvDescriptor.Alloc(allocator); }

  D3D12_CPU_DESCRIPTOR_HANDLE SrvDescriptorHandle() const { return m_SrvDescriptor.CpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE UavDescriptorHandle() const { return m_UavDescriptor.CpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE SrvDescriptorGpuHandle() const { return m_SrvDescriptor.GpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE UavDescriptorGpuHandle() const { return m_UavDescriptor.GpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE RtvDescriptorHandle() const { return m_RtvDescriptor.CpuHandle(); }

  void Attach(ID3D12Resource* other) { m_Resource.Attach(other); }

  Texture& Map() override
  {
    assert(!m_Mapped);

    CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, nullptr));
    m_Mapped = true;

    return *this;
  }

  Texture& Copy(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource = 0)
  {
    assert(m_Mapped);

    for (UINT i = 0; i < numSubresources; ++i) {
      m_Resource->WriteToSubresource(firstSubresource + i, nullptr, data[i].pData, static_cast<UINT>(data[i].RowPitch),
                                     static_cast<UINT>(data[i].SlicePitch));
    }

    return *this;
  }

private:
  HeapDescriptor m_RtvDescriptor;
};

}  // namespace Renderer
