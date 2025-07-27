#pragma once

#include "DescriptorHeapListAllocator.h"
#include "HeapDescriptor.h"

inline constexpr D3D12_RANGE EMPTY_RANGE = {0, 0};

struct GpuBuffer {
  ID3D12Resource* Resource() const { return resource.Get(); };

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(size_t offset = 0) const { return resource->GetGPUVirtualAddress() + offset; }

  void AllocSrvDescriptor(DescriptorHeapListAllocator& allocator) { srvDescriptor.Alloc(allocator); }

  void AllocSrvDescriptorWithGpuHandle(DescriptorHeapListAllocator& allocator)
  {
    srvDescriptor.AllocWithGpuHandle(allocator);
  }

  void AllocUavDescriptor(DescriptorHeapListAllocator& allocator) { uavDescriptor.Alloc(allocator); }

  void AllocUavDescriptorWithGpuHandle(DescriptorHeapListAllocator& allocator)
  {
    uavDescriptor.AllocWithGpuHandle(allocator);
  }

  void AllocRtvDescriptor(DescriptorHeapListAllocator& allocator) { rtvDescriptor.Alloc(allocator); }

  UINT SrvDescriptorIndex() const { return srvDescriptor.Index(); }

  UINT UavDescriptorIndex() const { return uavDescriptor.Index(); }

  D3D12_CPU_DESCRIPTOR_HANDLE SrvDescriptorHandle() const { return srvDescriptor.CpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE SrvDescriptorGpuHandle() const { return srvDescriptor.GpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE UavDescriptorHandle() const { return uavDescriptor.CpuHandle(); }

  D3D12_GPU_DESCRIPTOR_HANDLE UavDescriptorGpuHandle() const { return uavDescriptor.GpuHandle(); }

  D3D12_CPU_DESCRIPTOR_HANDLE RtvDescriptorHandle() const { return rtvDescriptor.CpuHandle(); }

  void Attach(ID3D12Resource* other) { resource.Attach(other); }

  void CreateResource(D3D12MA::Allocator* allocator,
                      const D3D12MA::ALLOCATION_DESC* allocDesc,
                      const D3D12_RESOURCE_DESC* pResourceDesc,
                      D3D12_RESOURCE_STATES InitialResourceState = D3D12_RESOURCE_STATE_COMMON,
                      const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr)
  {
    currentState = InitialResourceState;
    CHECK_HR(allocator->CreateResource(allocDesc, pResourceDesc, InitialResourceState, pOptimizedClearValue,
                                       &allocation, IID_PPV_ARGS(&resource)));
  }

  void Map() { CHECK_HR(resource->Map(0, &EMPTY_RANGE, &address)); }

  void Map(D3D12_RANGE* pReadRange, void** ppData) { CHECK_HR(resource->Map(0, pReadRange, ppData)); }

  void MapOpaque() { CHECK_HR(resource->Map(0, &EMPTY_RANGE, nullptr)); }

  void Unmap() { resource->Unmap(0, nullptr); }

  void Clear(size_t size) { ZeroMemory((BYTE*)address, size); }

  void Copy(size_t offset, const void* data, size_t size) { memcpy((BYTE*)address + offset, data, size); }

  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT DstSubresource = 0)
  {
    resource->WriteToSubresource(DstSubresource, nullptr, data->pData, data->RowPitch, data->SlicePitch);
  }

  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource = 0)
  {
    for (UINT i = 0; i < numSubresources; ++i) {
      resource->WriteToSubresource(firstSubresource + i, nullptr, data[i].pData, data[i].RowPitch, data[i].SlicePitch);
    }
  }

  D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
  {
    // TODO: implement state tracking assert(currentState == stateBefore);
    // maybe implement thin commandlist/queue wrapper?
    currentState = stateAfter;
    return CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), stateBefore, stateAfter);
  }

  void SetName(std::wstring name)
  {
    std::wstring allocName = name + L" (Allocation)";

    resourceName = name;
    resource->SetName(name.c_str());
    allocation->SetName(allocName.c_str());
  }

  void Reset()
  {
    resource.Reset();
    if (allocation) {
      allocation->Release();
      allocation = nullptr;
    }
  }

private:
  Microsoft::WRL::ComPtr<ID3D12Resource> resource;
  HeapDescriptor srvDescriptor;
  HeapDescriptor uavDescriptor;
  HeapDescriptor rtvDescriptor;
  D3D12MA::Allocation* allocation = nullptr;
  void* address = nullptr;
  D3D12_RESOURCE_STATES currentState;
  std::wstring resourceName;
};
