#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
  void DescriptorHeap::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS flags)
  {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{.Type = type,
                                        .NumDescriptors = numDescriptors,
                                        .Flags = flags};

    ID3D12DescriptorHeap* heap = nullptr;
    CHECK_HR(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)));
    m_Heap.Attach(heap);

    m_HeapStartCpu = m_Heap->GetCPUDescriptorHandleForHeapStart();

    if (heapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
      m_HeapStartGpu = m_Heap->GetGPUDescriptorHandleForHeapStart();
    }

    m_HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(type);
    m_NumDescriptors = numDescriptors;

    for (UINT i = 0; i < m_NumDescriptors; i++) m_FreeIndices.push_back(i);
  }

  DescriptorAllocation DescriptorHeap::Alloc()
  {
    assert(m_FreeIndices.size() > 0);
    // TODO: mutex

    UINT idx = m_FreeIndices.front();
    m_FreeIndices.pop_front();

    DescriptorAllocation alloc = {
      .index = idx,
      .cpuHandle = { .ptr = m_HeapStartCpu.ptr + (idx * m_HeapHandleIncrement) },
      .gpuHandle = { .ptr = m_HeapStartGpu.ptr + (idx * m_HeapHandleIncrement) },
    };

    return alloc;
  }

  void DescriptorHeap::Free(DescriptorAllocation alloc)
  {
    if (!alloc) return;

    assert(m_FreeIndices.size() < m_NumDescriptors);
    // TODO: mutex

    UINT cpuIdx = static_cast<UINT>((alloc.cpuHandle.ptr - m_HeapStartCpu.ptr) / m_HeapHandleIncrement);
    UINT gpuIdx = static_cast<UINT>((alloc.gpuHandle.ptr - m_HeapStartGpu.ptr) / m_HeapHandleIncrement);

    assert(cpuIdx == gpuIdx);
    m_FreeIndices.push_front(cpuIdx);
  }
}
