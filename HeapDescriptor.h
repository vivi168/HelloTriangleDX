#pragma once

struct HeapDescriptor {
  void Alloc(DescriptorHeapListAllocator& allocator)
  {
    m_Index = allocator.Alloc(&m_CpuHandle);
    m_CpuHandleSet = true;
    m_GpuHandleSet = false;
  }

  void AllocWithGpuHandle(DescriptorHeapListAllocator& allocator)
  {
    m_Index = allocator.Alloc(&m_CpuHandle, &m_GpuHandle);
    m_CpuHandleSet = true;
    m_GpuHandleSet = true;
  }

  UINT Index() const { return m_Index; }

  D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle() const
  {
    if (!m_CpuHandleSet) throw std::runtime_error("CPU Handle not set");
    return m_CpuHandle;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle() const
  {
    if (!m_GpuHandleSet) throw std::runtime_error("GPU Handle not set");
    return m_GpuHandle;
  }

private:
  UINT m_Index;
  D3D12_CPU_DESCRIPTOR_HANDLE m_CpuHandle;
  D3D12_GPU_DESCRIPTOR_HANDLE m_GpuHandle;
  bool m_CpuHandleSet = false;
  bool m_GpuHandleSet = false;
};
