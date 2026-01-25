#pragma once

// TODO:
// In a proper RHI we shouldn't expose API specifics in a frontfacing header!!!!
// have a front facing header that exposes common interfaces (eg IDevice, etc) contains api agnostic code.
// have api specific classes inheriting these interfaces and containing api specific code (eg, Device, etc)
#include <d3dx12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
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

// FIXME: duplicated for now with stdafx.h
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
#define CHECK_HR(expr)                                                             \
  do {                                                                             \
    if (FAILED(expr)) {                                                            \
      assert(0 && #expr);                                                          \
      throw std::runtime_error(__FILE__ "(" LINE_STRING "): FAILED( " #expr " )"); \
    }                                                                              \
  } while (false)

namespace IssouRHI
{
struct GPUSelection {
  UINT32 Index = UINT32_MAX;
  std::wstring Substring;
};

Microsoft::WRL::ComPtr<IDXGIFactory4> GetDXGIFactory();
void PrintAdapterList();
std::filesystem::path GetExecutableDirectory();

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

struct StageAccessLayout {
  // TODO: implement our own enums...
  D3D12_BARRIER_SYNC stage;
  D3D12_BARRIER_ACCESS access;
  D3D12_BARRIER_LAYOUT layout;
};

struct StageAccess {
  // TODO: implement our own enums...
  D3D12_BARRIER_SYNC stage;
  D3D12_BARRIER_ACCESS access;
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
  None = 0,
  CopySrc = 1 << 0,
  CopyDst = 1 << 1,
  TextureBinding = 1 << 2,
  StorageBinding = 1 << 3,
  RenderAttachment = 1 << 4,
};
ISSOURHI_ENUM_CLASS_OP(TextureUsage)

enum class TextureAspect : uint32_t {
  All,
  DepthOnly,
  StencilOnly,
};

enum class TextureFormat : uint32_t {
  BC5Unorm,
  BC7Unorm,
  Depth32Float,
  R8Unorm,
  RG8Unorm,
  R32Uint,
  RGBA8Unorm,
  RGB10A2Unorm,
  RGBA32Float,
  Undefined,
};

// TODO: where to put this
inline DXGI_FORMAT DXGIFormat(TextureFormat format)
{
  switch (format) {
  case TextureFormat::BC5Unorm:
    return DXGI_FORMAT_BC5_UNORM;
  case TextureFormat::BC7Unorm:
    return DXGI_FORMAT_BC7_UNORM;
  case TextureFormat::Depth32Float:
    return DXGI_FORMAT_D32_FLOAT;
  case TextureFormat::R8Unorm:
    return DXGI_FORMAT_R8_UNORM;
  case TextureFormat::RG8Unorm:
    return DXGI_FORMAT_R8G8_UNORM;
  case TextureFormat::R32Uint:
    return DXGI_FORMAT_R32_UINT;
  case TextureFormat::RGBA8Unorm:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case TextureFormat::RGB10A2Unorm:
    return DXGI_FORMAT_R10G10B10A2_UNORM;
  case TextureFormat::RGBA32Float:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  case TextureFormat::Undefined:
    return DXGI_FORMAT_UNKNOWN;
  }
}

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
  uint32_t mipLevelCount = 1;
  uint32_t baseArrayLayer = 0;
  uint32_t arrayLayerCount = 1;

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
  static D3D12_RESOURCE_DESC1 D3D12ResourceDesc(TextureDesc& desc);

  Texture(Device* device, TextureDesc& desc);
  ~Texture();

  // use std::expected (C++23) ?
  std::shared_ptr<TextureView> CreateView();
  std::shared_ptr<TextureView> CreateView(TextureViewDesc& desc);

  Device* GetDevice() const { return m_Device; }
  TextureUsage Usage() const { return m_Desc.usage; }
  TextureFormat Format() const { return m_Desc.format; }
public: // D3D12 impl specific
  void Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation = nullptr);
  void WriteToSubresource(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource = 0);

  // TODO: implement our own struct...
  std::optional<D3D12_TEXTURE_BARRIER> Transition(StageAccessLayout to);

  D3D12_SHADER_RESOURCE_VIEW_DESC SrvDescriptor(TextureViewDesc& desc) const;
  D3D12_UNORDERED_ACCESS_VIEW_DESC UavDescriptor(TextureViewDesc& desc) const;
  D3D12_RENDER_TARGET_VIEW_DESC RtvDescriptor(TextureViewDesc& desc) const;
  D3D12_DEPTH_STENCIL_VIEW_DESC DsvDescriptor(TextureViewDesc& desc) const;

  ID3D12Resource* Resource() const { return m_Resource.Get(); };
private:
  bool IsMultiSampled() const { return m_Desc.sampleCount > 1; }

  std::unordered_map<TextureViewDesc, std::shared_ptr<TextureView>, TextureViewDesc::Hasher> m_Views;

  Device* m_Device;
  TextureDesc m_Desc;
private:  // D3D12 impl specific
  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  D3D12MA::Allocation* m_Allocation = nullptr;

  StageAccessLayout m_CurrentStageAccessLayout;
};

class TextureView {
public:
  TextureView(Texture* tex, TextureViewDesc& desc);
  ~TextureView();
public: // D3D12 impl specific
  DescriptorAllocation SrvDescriptorAlloc() const { return m_Srv; }
  DescriptorAllocation UavDescriptorAlloc() const { return m_Uav; }
  DescriptorAllocation RtvDescriptorAlloc() const { return m_Rtv; }
  DescriptorAllocation DsvDescriptorAlloc() const { return m_Dsv; }

private:
  Texture* m_Texture; // should it be owning? weak ref?
  TextureViewDesc m_Desc;
  DescriptorAllocation m_Srv;
  DescriptorAllocation m_Uav;
  DescriptorAllocation m_Rtv;
  DescriptorAllocation m_Dsv;
};

enum class BufferUsage : uint32_t {
  None = 0,
  MapRead = 1 << 0,
  MapWrite = 1 << 1,
  CopySrc = 1 << 2,
  CopyDst = 1 << 3,
  Index = 1 << 4,
  Vertex = 1 << 5,
  Uniform = 1 << 6,
  Storage = 1 << 7,
  Indirect = 1 << 8,
  QueryResolve = 1 << 9,
  RayTracingAccelerationStructure = 1 << 10,
};
ISSOURHI_ENUM_CLASS_OP(BufferUsage)

struct BufferRange {
  uint64_t offset ;
  uint64_t size ;

  bool operator==(const BufferRange& other) const { return offset == other.offset && size == other.size; }
};
inline constexpr BufferRange FullBufferRange = {0, std::numeric_limits<uint64_t>::max()};

struct BufferDesc {
  std::string label;
  uint64_t size;
  BufferUsage usage;
};

class Buffer {
public:
  Buffer(Device* device, BufferDesc& desc);
  ~Buffer();

  uint64_t Size() const { return m_Desc.size; }
public: // D3D12 impl specific
  void Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation);
  // TODO: use enhanced barriers
  void InitState(D3D12_RESOURCE_STATES initialResourceState, bool fixedResourceState);

  void Write(BufferRange range, const void* data);
  void Clear(BufferRange range);
  void Read(BufferRange range, void* outData);

  D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress() const { return m_Resource->GetGPUVirtualAddress(); }

  DescriptorAllocation SrvDescriptorAlloc(BufferRange range, UINT byteStride);
  DescriptorAllocation UavDescriptorAlloc(BufferRange range, UINT byteStride, Buffer* counter = nullptr, UINT64 counterOffsetInBytes = 0);

  ID3D12Resource* Resource() const { return m_Resource.Get(); };

private:
  Device* m_Device;
  BufferDesc m_Desc;

  BufferRange ClampBufferRange(BufferRange range);

private: // D3D12 impl specific
  void Map();
  void Unmap();

  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  D3D12MA::Allocation* m_Allocation = nullptr;
  void* m_Address = nullptr;

  struct ViewKey {
    BufferRange range;
    UINT byteStride;
    Buffer* counter = nullptr;
    UINT64 counterOffsetInBytes = 0;

    bool operator==(const ViewKey& other) const
    {
      return range == other.range && byteStride == other.byteStride && counter == other.counter &&
             counterOffsetInBytes == other.counterOffsetInBytes;
    }

    struct Hasher {
      size_t operator()(const ViewKey& vk) const
      {
        size_t hash = 0;

        HashCombine(hash, vk.range.offset);
        HashCombine(hash, vk.range.size);
        HashCombine(hash, vk.byteStride);
        HashCombine(hash, reinterpret_cast<std::uintptr_t>(vk.counter));
        HashCombine(hash, vk.counterOffsetInBytes);

        return hash;
      }
    };
  };

  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Srvs{};
  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Uavs{};

  // TODO: use enhanced barriers
  D3D12_RESOURCE_STATES m_CurrentState = D3D12_RESOURCE_STATE_COMMON;
  bool m_FixedResourceState = false;
};

enum class ShaderStage : uint32_t {
  None = 0,
  Vertex = 1 << 0,
  Fragment = 1 << 1,
  Compute = 1 << 2,
  Mesh = 1 << 3,
  Raytracing = 1 << 4
};
ISSOURHI_ENUM_CLASS_OP(ShaderStage)

struct ShaderModule {
  ShaderModule(std::filesystem::path file)
  {
    std::ifstream inFile(file, std::ios::in | std::ios::binary | std::ios::ate);

    if (!inFile && file.is_relative()) {
      inFile.open(GetExecutableDirectory() / file, std::ios::in | std::ios::binary | std::ios::ate);
    }

    if (!inFile) throw std::runtime_error("Read ShaderModule");

    const std::streampos len = inFile.tellg();
    if (!inFile) throw std::runtime_error("Read ShaderModule");

    m_Code.resize(size_t(len));

    inFile.seekg(0, std::ios::beg);
    if (!inFile) throw std::runtime_error("Read ShaderModule");

    inFile.read(reinterpret_cast<char*>(m_Code.data()), len);
    if (!inFile) throw std::runtime_error("Read ShaderModule");

    inFile.close();
  }

  const uint8_t* Data() const { return m_Code.data(); }

  size_t Size() const { return m_Code.size(); }

private:
  std::vector<uint8_t> m_Code;
};

class PipelineBase
{
public:
  virtual ~PipelineBase() = default;

public:  // D3D12 impl specific
  void Attach(ID3D12PipelineState* pso);

private:  // D3D12 impl specific
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pso;
};

struct ComputePipelineDesc {
  std::string label;
  ShaderModule* module;
};

class ComputePipeline : public PipelineBase
{
public:
  ComputePipeline(Device* device, ComputePipelineDesc& desc);
  ~ComputePipeline();
private:
  Device* m_Device;
  ComputePipelineDesc m_Desc;
};

class Device
{
public:
  Device(const GPUSelection& gpuSelection);
  ~Device();

  void PrintAdapterInformation();

  // TODO: tmp. make interface.
  ID3D12Device5* GetNativeDevice() const { return m_Device.Get(); }
  D3D12MA::Allocator* GetAllocator() const { return m_Allocator.Get(); }
  ID3D12CommandQueue* GetNativeQueue() const { return m_CommandQueue.Get(); }

  std::shared_ptr<Texture> CreateTexture(TextureDesc& desc);
  std::shared_ptr<Buffer> CreateBuffer(BufferDesc& desc);

  std::shared_ptr<ComputePipeline> CreateComputePipeline(ComputePipelineDesc& desc);

  DescriptorAllocation AllocSrvUavDescriptor();
  DescriptorAllocation AllocRtvDescriptor();
  DescriptorAllocation AllocDsvDescriptor();

  void FreeSrvUavDescriptor(DescriptorAllocation alloc);
  void FreeRtvDescriptor(DescriptorAllocation alloc);
  void FreeDsvDescriptor(DescriptorAllocation alloc);

  ID3D12DescriptorHeap* SrvUavDescriptorHeap() const { return m_SrvUavDescriptorHeap.Get(); }
  ID3D12DescriptorHeap* RtvDescriptorHeap() const { return m_RtvDescriptorHeap.Get(); }
  ID3D12DescriptorHeap* DsvDescriptorHeap() const { return m_DsvDescriptorHeap.Get(); }
private:
  Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
  Microsoft::WRL::ComPtr<IDXGIFactory4> m_Factory;
  Microsoft::WRL::ComPtr<ID3D12Device5> m_Device;
  Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;
  // Used only when ENABLE_CPU_ALLOCATION_CALLBACKS
  D3D12MA::ALLOCATION_CALLBACKS m_AllocationCallbacks;

  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;  // TODO: our own Queue class here

  DescriptorHeap m_SrvUavDescriptorHeap;
  DescriptorHeap m_RtvDescriptorHeap;
  DescriptorHeap m_DsvDescriptorHeap;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
};

struct SurfaceConfiguration {
  TextureFormat format;
  UINT width;
  UINT height;
  UINT bufferCount;
  bool enableVsync = false;
};

class Surface
{
public:
  Surface(Device* device, HWND hwnd);
  ~Surface();

  void Configure(SurfaceConfiguration& config);
  std::shared_ptr<Texture> GetCurrentTexture();
  void Present();
  void WaitForAllFrames();

  UINT CurrentFrameIndex() const { return m_FrameIndex; }

private:
  void CreateSwapChain(SurfaceConfiguration& config);
  void CreateTextures(SurfaceConfiguration& config);

  void WaitFor(UINT64 fenceValue);

  bool m_EnableVsync = false;
  bool m_Configured = false;

  HWND m_Handle;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
  Device* m_Device;
  ID3D12CommandQueue* m_CommandQueue; // TODO: make this our Queue not native queue

  UINT m_FrameIndex;
  Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
  HANDLE m_FenceEvent = nullptr;
  UINT64 m_NextFenceValue = 0;

  std::vector<std::shared_ptr<Texture>> m_Textures;
  std::vector<UINT64> m_FenceValues;
};

class CommandEncoder;
class CommandQueue;

class RenderPass;
class ComputePass;
class RaytracingPass;

struct RenderPipelineDesc;
class RenderPipeline;

struct MeshPipelineDesc;
class MeshPipeline;

struct RaytracingPipelineDesc;
class RaytracingPipeline;


}  // namespace IssouRHI
