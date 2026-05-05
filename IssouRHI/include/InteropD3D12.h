#pragma once

#include "IssouRHI.h"

#ifdef BUILD_D3D12_BACKEND

#include <d3d12.h>

namespace IssouRHI
{
namespace D3D12
{
ID3D12Device* GetNativeDevice(Device* device);
ID3D12CommandQueue* GetNativeQueue(Queue* queue);
ID3D12GraphicsCommandList* GetNativeCommandList(CommandEncoder* encoder);

ID3D12DescriptorHeap* CbvSrvUavDescriptorHeap(Device* device);
ID3D12DescriptorHeap* RtvDescriptorHeap(Device* device);
ID3D12DescriptorHeap* DsvDescriptorHeap(Device* device);

void AllocCbvSrvUavDescriptor(Device* device, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);
void FreeCbvSrvUavDescriptor(Device* device, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

D3D12_CPU_DESCRIPTOR_HANDLE RtvDescriptorHandle(TextureView* view);
D3D12_CPU_DESCRIPTOR_HANDLE DsvDescriptorHandle(TextureView* view);

void PrintAdapterList();
void ReportLiveObjects();
}  // namespace D3D12
}  // namespace IssouRHI

#endif
