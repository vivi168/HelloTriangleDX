#include "InteropD3D12.h"

#include "CommandEncoderD3D12.h"
#include "DeviceD3D12.h"
#include "QueueD3D12.h"
#include "TextureD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
ID3D12Device* GetNativeDevice(Device* device)
{
  return ToBackend(device)->GetNativeDevice();
}

ID3D12CommandQueue* GetNativeQueue(Queue* queue)
{
  return ToBackend(queue)->GetNativeQueue();
}

ID3D12GraphicsCommandList* GetNativeCommandList(CommandEncoder* encoder)
{
  return static_cast<CommandEncoderImpl*>(encoder)->CommandList();
}

ID3D12DescriptorHeap* CbvSrvUavDescriptorHeap(Device* device)
{
  return ToBackend(device)->CbvSrvUavDescriptorHeap();
}

ID3D12DescriptorHeap* RtvDescriptorHeap(Device* device)
{
  return ToBackend(device)->RtvDescriptorHeap();
}

ID3D12DescriptorHeap* DsvDescriptorHeap(Device* device)
{
  return ToBackend(device)->DsvDescriptorHeap();
}

void AllocCbvSrvUavDescriptor(Device* device, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
{
  DescriptorAllocation allocation = ToBackend(device)->AllocCbvSrvUavDescriptor();

  if (cpuHandle) {
    *cpuHandle = allocation.cpuHandle;
  }
  if (gpuHandle) {
    *gpuHandle = allocation.gpuHandle;
  }
}

void FreeCbvSrvUavDescriptor(Device* device, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
{
  ToBackend(device)->FreeSrvUavDescriptor({
      .cpuHandle = cpuHandle,
      .gpuHandle = gpuHandle,
  });
}

D3D12_CPU_DESCRIPTOR_HANDLE RtvDescriptorHandle(TextureView* view)
{
  return ToBackend(view)->RtvDescriptorAlloc().cpuHandle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DsvDescriptorHandle(TextureView* view)
{
  return ToBackend(view)->DsvDescriptorAlloc().cpuHandle;
}
}  // namespace D3D12
}  // namespace IssouRHI
