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
#include <unordered_map>

#include <cassert>
#include <cstdint>

#include <D3D12MemAlloc.h>

#ifdef _DEBUG
#define ENABLE_DEBUG_LAYER true
#endif

#define ISSOURHI_ENUM_CLASS_OP(e) \
  static_assert(sizeof(e) <= sizeof(uint32_t)); \
  constexpr e operator ~ (e a) { return (e)(~(uint32_t)a); } \
  constexpr uint32_t operator & (e a, e b) { return (uint32_t)a & (uint32_t)b; } \
  constexpr e operator | (e a, e b) { return (e)((uint32_t)a | (uint32_t)b); } \
  constexpr e& operator &= (e& a, e b) { a = (e)(a & b); return a; } \
  constexpr e& operator |= (e& a, e b) { a = (e)(a | b); return a; }

namespace IssouRHI
{
struct GPUSelection {
  UINT32 Index = UINT32_MAX;
  std::wstring Substring;
};

Microsoft::WRL::ComPtr<IDXGIFactory4> GetDXGIFactory();
void PrintAdapterList();


struct DescriptorAllocation {
  UINT index;
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
};

class DescriptorHeap
{
public:
  void Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_FLAGS flags);
  DescriptorAllocation Alloc();
  void Free(DescriptorAllocation alloc);
  void Free(UINT index);

private:
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_Heap;

  D3D12_CPU_DESCRIPTOR_HANDLE m_HeapStartCpu{};
  D3D12_GPU_DESCRIPTOR_HANDLE m_HeapStartGpu{};
  UINT m_HeapHandleIncrement;
  UINT m_NumDescriptors;
  std::deque<UINT> m_FreeIndices;

  // TODO: mutex
};

enum class TextureDimension : uint32_t {
  Texture1D,
  Texture2D,
  Texture3D,
};

enum class TextureViewDimension : uint32_t {
  Texture1D,
  Texture2D,
  Texture2DAry,
  TextureCube,
  TextureCubeAry,
  Texture3D,
};

enum class TextureUsage : uint32_t {
  CopySrc = 1 << 0,
  CopyDst = 1 << 1,
  TextureBinding = 1 << 2,
  StorageBinding = 1 << 3,
  RenderAttachment = 1 << 4,
};
ISSOURHI_ENUM_CLASS_OP(TextureUsage)

enum class TextureAspect : uint32_t {
  All,
  StencilOnly,
  DepthOnly,
};

enum class TextureFormat : uint32_t {
  Depth32Float,
  R8Unorm,
  RG8Unorm,
  R32Uint,
  RGBA8Unorm,
  RGB10A2Unorm,
  RGBA32Float,
  Undefined,
};

enum class VertexFormat : uint32_t {
  Float32x3,
};

struct Extent3D {
  uint32_t width;
  uint32_t height = 1;
  uint32_t depth = 1;
};

struct TextureDesc {
  std::string label;
  Extent3D size;
  uint32_t mipLevelCount = 1;
  uint32_t sampleCount = 1;
  TextureDimension dimension = TextureDimension::Texture2D;
  TextureFormat format;
  TextureUsage usage;
};

inline void HashCombine(size_t& seed, const size_t value)
{
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct SubresourceRange {
  uint32_t baseMipLevel = 0;
  uint32_t mipLevelCount;
  uint32_t baseArrayLayer = 0;
  uint32_t arrayLayerCount;

  bool operator == (const SubresourceRange& other) const
  {
    return baseMipLevel == other.baseMipLevel &&
        mipLevelCount == other.mipLevelCount &&
        baseArrayLayer == other.baseArrayLayer &&
        arrayLayerCount == other.arrayLayerCount;
  }
};

struct TextureViewDesc {
  TextureFormat format;
  TextureViewDimension dimension;
  TextureAspect aspect = TextureAspect::All;
  SubresourceRange range;

  bool operator == (const TextureViewDesc& other) const
  {
    return format == other.format &&
        dimension == other.dimension &&
        aspect == other.aspect &&
        range == other.range;
  }

  struct Hasher {
    size_t operator () (const TextureViewDesc& desc) const
    {
      size_t hash = 0;

      HashCombine(hash, static_cast<uint32_t>(desc.format));
      HashCombine(hash, static_cast<uint32_t>(desc.dimension));
      HashCombine(hash, static_cast<uint32_t>(desc.aspect));
      HashCombine(hash, desc.range.baseMipLevel);
      HashCombine(hash, desc.range.mipLevelCount);
      HashCombine(hash, desc.range.baseArrayLayer);
      HashCombine(hash, desc.range.arrayLayerCount);

      return hash;
    }
  };
};

// forward decl.
// should be useless one we split files correctly (public interface + backend specific)
class Device;
class TextureView;

class Texture {
public:
  static D3D12_RESOURCE_DESC D3D12ResourceDesc(TextureDesc& desc);

  Texture(Device* device, TextureDesc& desc);
  ~Texture();

  std::shared_ptr<TextureView> CreateView(TextureViewDesc& desc);

  void Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation = nullptr);
  void Copy(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource = 0);

  D3D12_SHADER_RESOURCE_VIEW_DESC SrvDescriptor(TextureViewDesc& desc) const;
  D3D12_UNORDERED_ACCESS_VIEW_DESC UavDescriptor(TextureViewDesc& desc) const;
  D3D12_RENDER_TARGET_VIEW_DESC RtvDescriptor(TextureViewDesc& desc) const;
  D3D12_DEPTH_STENCIL_VIEW_DESC DsvDescriptor(TextureViewDesc& desc) const;
private:
  std::unordered_map<TextureViewDesc, std::shared_ptr<TextureView>, TextureViewDesc::Hasher> m_Views;

  void Map();
  void Unmap();

  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  D3D12MA::Allocation* m_Allocation = nullptr;
  Device* m_Device;
  TextureDesc m_Desc;
  boolean m_Mapped = false;
};

class TextureView {
public:
  TextureView(Texture* tex, TextureViewDesc& desc);
  ~TextureView();

  D3D12_CPU_DESCRIPTOR_HANDLE SrvDescriptorHandle();
  D3D12_CPU_DESCRIPTOR_HANDLE UavDescriptorHandle();
  D3D12_CPU_DESCRIPTOR_HANDLE RtvDescriptorHandle();
  D3D12_CPU_DESCRIPTOR_HANDLE DsvDescriptorHandle();

  UINT SrvDescriptorIndex();
  UINT UavDescriptorIndex();
  UINT RtvDescriptorIndex();
  UINT DsvDescriptorIndex();

private:
  Texture* m_Texture;
  TextureViewDesc m_Desc;
  DescriptorAllocation m_Srv;
  DescriptorAllocation m_Uav;
  DescriptorAllocation m_Rtv;
  DescriptorAllocation m_Dsv;
};

struct BufferDesc;
class Buffer;

class Device
{
public:
  Device(const GPUSelection& gpuSelection);
  ~Device();

  void PrintAdapterInformation();

  // TODO: tmp. make interface.
  Microsoft::WRL::ComPtr<ID3D12Device5> GetNativeDevice() const { return m_Device; }
  D3D12MA::Allocator* GetAllocator() const { return m_Allocator.Get(); }
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> GetCommandQueue() const { return m_CommandQueue; }

  std::shared_ptr<Texture> CreateTexture(TextureDesc& desc);

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

struct SurfaceConfiguration {
  TextureFormat format;
  DXGI_SWAP_EFFECT swapEffect;  // TODO: expose our own API agnostic enum
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
  Texture* GetCurrentTexture();
  void Present();

private:
  void CreateSwapChain(SurfaceConfiguration& desc);
  void CreateTextures(SurfaceConfiguration& desc);

  bool m_Configured = false;

  HWND m_Handle;
  // TODO: should we roll our own RefCountPtr instead of using shared_ptr?
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
