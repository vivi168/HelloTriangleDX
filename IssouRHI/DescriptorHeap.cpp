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
  }

  DescriptorHeap::~DescriptorHeap() { m_Heap.Reset(); }
}
