#pragma once

#include "CommonD3D12.h"

#include <deque>

namespace IssouRHI
{
namespace D3D12
{
struct DescriptorAllocation {
  UINT index;
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};

  explicit operator bool() const { return cpuHandle.ptr != 0; }
};

class DescriptorHeap
{
public:
  void Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS flags);
  DescriptorAllocation Alloc();
  void Free(DescriptorAllocation alloc);

  // FIXME: TMP
  ID3D12DescriptorHeap* Get() const { return m_Heap.Get(); }

private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;

  D3D12_CPU_DESCRIPTOR_HANDLE m_HeapStartCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE m_HeapStartGpu{};
  UINT m_HeapHandleIncrement;
  UINT m_NumDescriptors;
  std::deque<UINT> m_FreeIndices;

  // TODO: mutex
};
}
}
