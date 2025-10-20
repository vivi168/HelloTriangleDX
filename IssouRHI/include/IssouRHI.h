#pragma once

// TODO:
// In a proper RHI we shouldn't expose API specifics in a frontfacing header!!!!
// have a front facing header that exposes common interfaces (eg IDevice, etc) contains api agnostic code.
// have api specific classes inheriting these interfaces and containing api specific code (eg, Device, etc)
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

#include <cassert>

#include <D3D12MemAlloc.h>

#ifdef _DEBUG
#define ENABLE_DEBUG_LAYER true
#endif

namespace IssouRHI
{
struct GPUSelection {
  UINT32 Index = UINT32_MAX;
  std::wstring Substring;
};

Microsoft::WRL::ComPtr<IDXGIFactory4> GetDXGIFactory();
void PrintAdapterList();

class DescriptorAllocation
{
  UINT Index() const { return m_Index; }

  D3D12_GPU_DESCRIPTOR_HANDLE NativeHandle() const
  {
    if (m_Handle.ptr == 0) throw std::runtime_error("Handle not set");

    return m_Handle;
  }

private:
  UINT m_Index;
  D3D12_GPU_DESCRIPTOR_HANDLE m_Handle = {0};
};

class DescriptorHeap
{
public:
  void Create(ID3D12Device* device,
              D3D12_DESCRIPTOR_HEAP_TYPE type,
              UINT numDescriptors,
              D3D12_DESCRIPTOR_HEAP_FLAGS flags);
  ~DescriptorHeap();

  DescriptorAllocation Alloc();
  void Free(UINT index);

private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;
  D3D12_CPU_DESCRIPTOR_HANDLE m_HeapStartCpu = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE m_HeapStartGpu = {0};

  std::deque<UINT> m_FreeIndices;
  UINT m_HeapHandleIncrement;
  UINT m_NumDescriptors;
};

class Device
{
public:
  Device(const GPUSelection& gpuSelection);
  ~Device();

  void PrintAdapterInformation();

  // TODO: tmp. make interface
  Microsoft::WRL::ComPtr<ID3D12Device5> GetNativeDevice() const { return m_Device; }
  D3D12MA::Allocator* GetAllocator() const { return m_Allocator.Get(); }
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetCommandQueue() const { return m_CommandQueue; }

private:
  Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
  Microsoft::WRL::ComPtr<IDXGIFactory4> m_Factory;
  Microsoft::WRL::ComPtr<ID3D12Device5> m_Device;
  Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;
  // Used only when ENABLE_CPU_ALLOCATION_CALLBACKS
  D3D12MA::ALLOCATION_CALLBACKS m_AllocationCallbacks;

  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;  // TODO: wrapper for this

  DescriptorHeap m_SrvUavDescriptorHeap;
  DescriptorHeap m_RtvDescriptorHeap;
  DescriptorHeap m_DepthStencilDescriptorHeap;
};

struct TextureDesc;
class Texture;
struct TextureViewDesc;
class TextureView;

struct BufferDesc;
class Buffer;
struct BufferViewDesc;
class BufferView;

struct SurfaceConfiguration {
  DXGI_FORMAT format;
  DXGI_SWAP_EFFECT swapEffect;
  UINT width;
  UINT height;
  UINT bufferCount;
};

class Surface
{
public:
  Surface(Device& device, HWND hwnd);
  ~Surface();

  void Configure(SurfaceConfiguration& desc);
  std::shared_ptr<Texture> GetCurrentTexture();
  void Present();

private:
  void CreateSwapChain(SurfaceConfiguration& desc);
  void CreateTextures(SurfaceConfiguration& desc);

  bool m_Configured = false;

  HWND m_Handle;
  std::shared_ptr<Device> m_Device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
  UINT m_FrameIndex;
  Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
  std::vector<HANDLE> m_FenceEvent;
  UINT64 m_FenceValue = 0;

  std::vector<std::shared_ptr<Texture>> m_Textures;
};

class CommandEncoder;
class CommandQueue;

class RenderPass;
class ComputePass;
class RaytracingPass;

struct RenderPipelineDesc;
class RenderPipeline;

struct ComputePipelineDesc;
class ComputePipeline;

struct RaytracingPipelineDesc;
class RaytracingPipeline;


}  // namespace IssouRHI
