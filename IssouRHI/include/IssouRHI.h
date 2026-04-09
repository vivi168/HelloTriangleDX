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
#include <span>
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

#define ISSOURHI_BIT(b) (1 << (b))
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
// TODO: return std::expected for function that call CHECK_HR?
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

void ReportLiveObjects();
void PrintAdapterList();

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

class Device;

enum class QueryType {
  Timestamp,
};

struct QuerySetDesc {
  std::string label;
  QueryType type;
  uint32_t count;
};

class QuerySet
{
public:
  QuerySet(Device* device, const QuerySetDesc& desc);
  ~QuerySet();

  void Create();
public:
  ID3D12QueryHeap* QueryHeap() const { return m_QueryHeap.Get(); }
private:
  Device* m_Device;
  QuerySetDesc m_Desc;
private:
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_QueryHeap;
};

struct TimestampWrites {
  uint32_t beginningOfPassWriteIndex;
  uint32_t endOfPassWriteIndex;

  QuerySet* querySet;
};
inline constexpr uint32_t QuerySetIndexUndefined = std::numeric_limits<uint32_t>::max();

enum class PipelineStage : uint32_t {
  None = 0,
  All = std::numeric_limits<uint32_t>::max(),
  // Graphics
  IndexInput = ISSOURHI_BIT(0),
  VertexShader = ISSOURHI_BIT(1),
  TaskShader = ISSOURHI_BIT(2),
  MeshShader = ISSOURHI_BIT(3),
  FragmentShader = ISSOURHI_BIT(4),
  DepthStencilAttachment = ISSOURHI_BIT(5),
  ColorAttachment = ISSOURHI_BIT(6),
  // Compute
  ComputeShader = ISSOURHI_BIT(7),
  // Ray Tracing
  RaygenShader = ISSOURHI_BIT(8),
  MissShader = ISSOURHI_BIT(9),
  IntersectionShader = ISSOURHI_BIT(10),
  ClosestHitShader = ISSOURHI_BIT(11),
  AnyHitShader = ISSOURHI_BIT(12),
  CallableShader = ISSOURHI_BIT(13),
  AccelerationStructure = ISSOURHI_BIT(14),
  Micromap = ISSOURHI_BIT(15),
  // Indirect
  Indirect = ISSOURHI_BIT(16),
  // Copy, Resolve, Clear
  Copy = ISSOURHI_BIT(17),
  Resolve = ISSOURHI_BIT(18),
  ClearStorage = ISSOURHI_BIT(19),
  // Meta
  MeshShaders = TaskShader | MeshShader,
  RayTracingShaders = RaygenShader | MissShader | IntersectionShader | ClosestHitShader | AnyHitShader | CallableShader,
};
ISSOURHI_ENUM_CLASS_OP(PipelineStage);

enum class Access : uint32_t {
  None = 0,
  // Buffer
  IndexBuffer = ISSOURHI_BIT(0),
  VertexBuffer = ISSOURHI_BIT(1),
  ConstantBuffer = ISSOURHI_BIT(2),
  ArgumentBuffer = ISSOURHI_BIT(3),
  ScratchBuffer = ISSOURHI_BIT(4),
  // Attachment
  ColorAttachmentRead = ISSOURHI_BIT(5),
  ColorAttachmentWrite = ISSOURHI_BIT(6),
  DepthStencilAttachmentRead = ISSOURHI_BIT(7),
  DepthStencilAttachmentWrite = ISSOURHI_BIT(8),
  ShadingRateAttachment = ISSOURHI_BIT(9),
  InputAttachment = ISSOURHI_BIT(10),
  // Acceleration Structures
  AccelerationStructureRead = ISSOURHI_BIT(11),
  AccelerationStructureWrite = ISSOURHI_BIT(12),
  // Micromap
  MicromapRead = ISSOURHI_BIT(13),
  MicromapWrite = ISSOURHI_BIT(14),
  // Shader Resources
  ShaderResource = ISSOURHI_BIT(15),
  ShaderResourceStorage = ISSOURHI_BIT(16),
  ShaderBindingTable = ISSOURHI_BIT(17),
  // Copy
  CopySource = ISSOURHI_BIT(18),
  CopyDestination = ISSOURHI_BIT(19),
  // Resolve
  ResolveSource = ISSOURHI_BIT(20),
  ResolveDestination = ISSOURHI_BIT(21),
  // Clear
  ClearStorage = ISSOURHI_BIT(22),
};
ISSOURHI_ENUM_CLASS_OP(Access);

enum class TextureLayout {
  Undefined,
  General,
  Present,
  // Attachment
  ColorAttachment,
  DepthStencilAttachment,
  DepthReadonlyStencilAttachment,
  DepthAttachmentStencilReadonly,
  DepthStencilReadonly,
  ShadingRateAttachment,
  InputAttachment,
  // Shader Resources
  ShaderResource,
  ShaderResourceStorage,
  // Copy
  CopySource,
  CopyDestination,
  // Resolve
  ResolveSource,
  ResolveDestination,
};

struct StageAccessLayout {
  // TODO: implement our own enums...
  PipelineStage stage;
  Access access;
  TextureLayout layout;
};

struct StageAccess {
  // TODO: implement our own enums...
  PipelineStage stage;
  Access access;
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

// TODO: add missing types
enum class TextureFormat : uint32_t {
  Undefined,
  BC5Unorm,
  BC7Unorm,
  Depth32Float,
  R8Unorm,
  RG8Unorm,
  R32Uint,
  RGBA8Unorm,
  RGB10A2Unorm,
  RGBA32Float,
};

// TODO: where to put this
inline DXGI_FORMAT DXGIFormat(TextureFormat format)
{
  switch (format) {
  case TextureFormat::Undefined:
    return DXGI_FORMAT_UNKNOWN;
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
  }
}

enum class VertexFormat : uint32_t {
  Float32x3,
};

struct Extent3D {
  uint32_t width;
  uint32_t height = 1;
  uint32_t depth = 1;

  bool operator == (const Extent3D& other) const
  {
    return width == other.width && height == other.height && depth == other.depth;
  }
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
// for class that have a m_Device, m_Label, inherit from a common Base.
// Base <- Interface (public interface impl) <- Impl ( <- API specific impl)
class TextureView;

class Texture {
public:
  static D3D12_RESOURCE_DESC1 D3D12ResourceDesc(const TextureDesc& desc);

  Texture(Device* device, const TextureDesc& desc);
  ~Texture();

  // use std::expected (C++23) ?
  std::shared_ptr<TextureView> CreateView();
  std::shared_ptr<TextureView> CreateView(const TextureViewDesc& desc);

  Device* GetDevice() const { return m_Device; }
  TextureUsage Usage() const { return m_Desc.usage; }
  TextureFormat Format() const { return m_Desc.format; }
  Extent3D Size() const { return m_Desc.size; };
  Extent3D SizeAtMipLevel(uint32_t level) const;
public: // D3D12 impl specific
  void Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation = nullptr);
  void WriteToSubresource(D3D12_SUBRESOURCE_DATA* data, UINT numSubresources, UINT firstSubresource = 0);

  D3D12_SHADER_RESOURCE_VIEW_DESC SrvDescriptor(const TextureViewDesc& desc) const;
  D3D12_UNORDERED_ACCESS_VIEW_DESC UavDescriptor(const TextureViewDesc& desc) const;
  // Internal use (OMSetRenderTargets, ClearRenderTargetView, etc)
  D3D12_RENDER_TARGET_VIEW_DESC RtvDescriptor(const TextureViewDesc& desc) const;
  D3D12_DEPTH_STENCIL_VIEW_DESC DsvDescriptor(const TextureViewDesc& desc) const;

  ID3D12Resource* Resource() const { return m_Resource.Get(); };
private:
  bool IsMultiSampled() const { return m_Desc.sampleCount > 1; }

  std::unordered_map<TextureViewDesc, std::shared_ptr<TextureView>, TextureViewDesc::Hasher> m_Views;

  Device* m_Device;
  TextureDesc m_Desc;
private:  // D3D12 impl specific
  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  D3D12MA::Allocation* m_Allocation = nullptr;
};

enum class TextureAccess { Read, ReadWrite };

class TextureView {
public:
  TextureView(Texture* tex, const TextureViewDesc& desc);
  ~TextureView();

  uint32_t DescriptorIndex(TextureAccess access) const;
  uint64_t DescriptorHandle(TextureAccess access) const;

  Extent3D Size() const { return m_Texture->SizeAtMipLevel(m_Desc.range.baseMipLevel); }
public: // D3D12 impl specific
  // Internal use
  DescriptorAllocation SrvDescriptorAlloc() const { return m_Srv; }
  DescriptorAllocation UavDescriptorAlloc() const { return m_Uav; }
  // Internal use (OMSetRenderTargets, ClearRenderTargetView, etc)
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
  uint64_t offset;
  uint64_t size;

  bool operator==(const BufferRange& other) const { return offset == other.offset && size == other.size; }
};
inline constexpr BufferRange FullBufferRange = {0, std::numeric_limits<uint64_t>::max()};

struct BufferDesc {
  std::string label;
  uint64_t size;
  BufferUsage usage;
};

enum class BufferAccess { Constant, Read, ReadWrite };

class Buffer;

struct BufferViewDesc {
  BufferAccess access;
  BufferRange range = FullBufferRange;
  uint32_t elementStride = 0;
  size_t counterOffset = 0;
  Buffer* counter = nullptr;
};

class Buffer {
public:
  Buffer(Device* device, const BufferDesc& desc);
  ~Buffer();

  uint64_t Size() const { return m_Desc.size; }
public:
  uint32_t DescriptorIndex(const BufferViewDesc& desc);
  // FIXME: sort methods below by correct "ownership"
public: // D3D12 impl specific
  void Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation);

  void Write(BufferRange range, const void* data);
  void Clear(BufferRange range);
  void Read(BufferRange range, void* outData);

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress() const { return m_Resource->GetGPUVirtualAddress(); }

  ID3D12Resource* Resource() const { return m_Resource.Get(); };

private:
  BufferRange ClampBufferRange(BufferRange range);

  DescriptorAllocation CbvDescriptorAlloc(BufferRange range);
  DescriptorAllocation SrvDescriptorAlloc(BufferRange range, UINT byteStride);
  DescriptorAllocation UavDescriptorAlloc(BufferRange range, UINT byteStride, Buffer* counter, UINT64 counterOffsetInBytes);

  Device* m_Device;
  BufferDesc m_Desc;
private: // D3D12 impl specific
  void Map();
  void Unmap();

  Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;
  D3D12MA::Allocation* m_Allocation = nullptr;
  void* m_Address = nullptr;

  struct ViewKey {
    BufferRange range;
    UINT byteStride = 0;
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

  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Cbvs{};
  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Srvs{};
  std::unordered_map<ViewKey, DescriptorAllocation, ViewKey::Hasher> m_Uavs{};
};

enum class ShaderStage : uint32_t {
  None = 0,
  Vertex = 1 << 0,
  Fragment = 1 << 1,
  Compute = 1 << 2,
  Mesh = 1 << 3,
  RayTracing = 1 << 4
};
ISSOURHI_ENUM_CLASS_OP(ShaderStage)

struct ShaderModule {
  const void* code;
  size_t size;
};

class PipelineBase
{
public:
  PipelineBase(Device* device);
  virtual ~PipelineBase();

  virtual void Create() = 0;

public:
  ID3D12PipelineState* PipelineState() const { return m_Pso.Get(); }
protected:  // D3D12 impl specific
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pso;

  Device* m_Device;
};

struct ComputePipelineDesc {
  std::string label;
  ShaderModule* computeModule;
};

class ComputePipeline : public PipelineBase
{
public:
  ComputePipeline(Device* device, const ComputePipelineDesc& desc);
  ~ComputePipeline();

  void Create() override;
private:
  ComputePipelineDesc m_Desc;
};

enum ColorWriteFlags : uint8_t {
  Red = ISSOURHI_BIT(0),
  Green = ISSOURHI_BIT(1),
  Blue = ISSOURHI_BIT(2),
  Alpha = ISSOURHI_BIT(3),
  All = Red | Green | Blue | Alpha,
};
ISSOURHI_ENUM_CLASS_OP(ColorWriteFlags)

enum class BlendOperation { Add, Subtract, ReverseSubtract, Min, Max };

enum class BlendFactor {
  Zero,
  One,
  Src,
  OneMinusSrc,
  SrcAlpha,
  OneMinusSrcAlpha,
  Dst,
  OneMinusDst,
  DstAlpha,
  OneMinusDstAlpha,
  SrcAlphaSaturated,
  Constant,
  OneMinusConstant,
  Src1,
  OneMinusSrc1,
  Src1Alpha,
  OneMinusSrc1Alpha,
};

struct BlendComponent {
  BlendOperation operation = BlendOperation::Add;
  BlendFactor srcFactor = BlendFactor::One;
  BlendFactor dstFactor = BlendFactor::Zero;
};

struct BlendState {
  BlendComponent color;
  BlendComponent alpha;
};

struct ColorTargetState {
  TextureFormat format;
  std::optional<BlendState> blend;
  ColorWriteFlags writeMask = ColorWriteFlags::All;
};

enum class CompareFunction {
  Never,
  Less,
  Equal,
  LessEqual,
  Greater,
  NotEqual,
  GreaterEqual,
  Always,
};

enum class StencilOperation {
  Keep,
  Zero,
  Replace,
  Invert,
  IncrementClamp,
  DecrementClamp,
  IncrementWrap,
  DecrementWrap,
} ;

struct StencilFaceState {
  CompareFunction compare = CompareFunction::Always;
  StencilOperation depthFailOp = StencilOperation::Keep;
  StencilOperation failOp = StencilOperation::Keep;
  StencilOperation passOp = StencilOperation::Keep;

  bool Enabled() const
  {
    return compare != CompareFunction::Always ||
           depthFailOp != StencilOperation::Keep ||
           failOp != StencilOperation::Keep ||
           passOp != StencilOperation::Keep;
  }
};

struct DepthStencilState {
  TextureFormat format;
  // Depth
  int32_t depthBias = 0;
  float depthBiasClamp = 0.f;
  float depthBiasSlopeScale = 0.f;
  CompareFunction depthCompare = CompareFunction::Always;
  bool depthWriteEnabled = false;
  // Stencil
  StencilFaceState stencilFront;
  StencilFaceState stencilBack;
  uint32_t stencilReadMask = 0xFFFFFFFF;
  uint32_t stencilWriteMask = 0xFFFFFFFF;
};

enum class PrimitiveTopology {
  PointList,
  LineList,
  LineStrip,
  TriangleList,
  TriangleStrip,
};

enum class IndexFormat { Uint16, Uint32 };

enum class FrontFace { CCW, CW };

enum class CullMode { None, Front, Back };

struct PrimitiveState {
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  IndexFormat stripIndexFormat;
  FrontFace frontFace = FrontFace::CCW;
  CullMode cullMode = CullMode::None;
  bool unclippedDepth = false;
};

struct MultisampleState {
  uint32_t count = 1;
  uint32_t mask = std::numeric_limits<uint32_t>::max();
  bool alphaToCoverageEnabled = false;
};

struct RenderPipelineDesc {
  std::string label;
  ShaderModule* vertexModule;
  ShaderModule* fragmentModule;
  std::span<ColorTargetState> targets;
  DepthStencilState depthStencil;
  PrimitiveState primitive;
  MultisampleState multiSample;
};

class RenderPipeline : public PipelineBase
{
public:
  RenderPipeline(Device* device, const RenderPipelineDesc& desc);
  ~RenderPipeline();

  void Create() override;
public:
  D3D12_PRIMITIVE_TOPOLOGY NativePrimitiveTopology() const { return m_PrimitiveTopology; }
private:
  RenderPipelineDesc m_Desc;
private:
  D3D12_PRIMITIVE_TOPOLOGY m_PrimitiveTopology;
};

struct MeshPipelineDesc;
class MeshPipeline;

struct RayTracingPipelineDesc;
class RayTracingPipeline;

class Queue;

class Device
{
public:
  Device(const GPUSelection& gpuSelection);
  ~Device();

  void PrintAdapterInformation();

  Queue* GetQueue() const { return m_Queue.get(); }

  std::shared_ptr<QuerySet> CreateQuerySet(const QuerySetDesc &desc);
  std::shared_ptr<Texture> CreateTexture(const TextureDesc& desc);
  std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc);

  std::shared_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc);
  std::shared_ptr<RenderPipeline> CreateRenderPipeline(const RenderPipelineDesc& desc);

  DescriptorAllocation AllocCbvSrvUavDescriptor();
  DescriptorAllocation AllocRtvDescriptor();
  DescriptorAllocation AllocDsvDescriptor();

  void FreeSrvUavDescriptor(DescriptorAllocation alloc);
  void FreeRtvDescriptor(DescriptorAllocation alloc);
  void FreeDsvDescriptor(DescriptorAllocation alloc);

public:
  ID3D12Device5* GetNativeDevice() const { return m_Device.Get(); }
  IDXGIAdapter1* GetAdapter() const { return m_Adapter.Get(); }
  D3D12MA::Allocator* GetAllocator() const { return m_Allocator.Get(); }

  ID3D12DescriptorHeap* CbvSrvUavDescriptorHeap() const { return m_CbvSrvUavDescriptorHeap.Get(); }
  ID3D12DescriptorHeap* RtvDescriptorHeap() const { return m_RtvDescriptorHeap.Get(); }
  ID3D12DescriptorHeap* DsvDescriptorHeap() const { return m_DsvDescriptorHeap.Get(); }

  ID3D12RootSignature* RootSignature() const { return m_RootSignature.Get(); }
private:
  std::unique_ptr<Queue> m_Queue;

  DescriptorHeap m_CbvSrvUavDescriptorHeap;
  DescriptorHeap m_RtvDescriptorHeap;
  DescriptorHeap m_DsvDescriptorHeap;
private:
  Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
  Microsoft::WRL::ComPtr<ID3D12Device5> m_Device;
  Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;
  // Used only when ENABLE_CPU_ALLOCATION_CALLBACKS
  D3D12MA::ALLOCATION_CALLBACKS m_AllocationCallbacks;

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

class CommandBuffer
{
public:
  CommandBuffer(Device* device);
  ~CommandBuffer();

  void Create();
  void Init();
  void Reset();
public:
  void UpdateFenceValue(UINT64 value) { m_FenceValue = value; }
  UINT64 FenceValue() const { return m_FenceValue; }

  void SetComputeRootSignatureIfNeeded();
  void SetGraphicsRootSignatureIfNeeded();

  ID3D12GraphicsCommandList8* CommandList() const { return m_CommandList.Get(); }
private:
  Device* m_Device;
private:
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList8> m_CommandList;

  UINT64 m_FenceValue = 0;
  bool m_ComputeRootSignatureSet = false;
  bool m_GraphicRootSignatureSet = false;

  // TODO: keep track of pso change
  // TODO: basic resource state tracking?
};

class Queue
{
public:
  Queue(Device* device);
  ~Queue();

  void Create();

  CommandEncoder CreateCommandEncoder(std::optional<std::string> label = std::nullopt);

  void Submit(std::span<CommandBuffer*> commandBuffers);
  void WaitForAll();
public:
  // TMP: remove this once public API is done
  ID3D12CommandQueue* GetNativeQueue() const { return m_CommandQueue.Get(); }
private:
  CommandBuffer* FindOrCreateCommandBuffer();
  CommandBuffer* CreateCommandBuffer();
  void RecycleCommandBuffers();
  void InitCommandBuffer(CommandBuffer* commandBuffer);
  void ResetCommandBuffer(CommandBuffer* commandBuffer);

  Device* m_Device;

  // TODO: mutex to protect these
  std::vector<std::unique_ptr<CommandBuffer>> m_CommandBuffers;
  std::list<CommandBuffer*> m_CommandBuffersExecuting;
  std::list<CommandBuffer*> m_CommandBuffersAvailable;
private:
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;

  Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
  UINT64 m_NextFenceValue = 0;
  HANDLE m_FenceEvent = nullptr;
};

class ComputePassEncoder;
class RenderPassEncoder;
class MeshPassEncoder;
class RayTracingPassEncoder;

struct ComputePassDesc;
struct RenderPassDesc;

class EncoderBase
{
public:
  EncoderBase(CommandBuffer* commandBuffer);
  virtual ~EncoderBase();
public:
  ID3D12GraphicsCommandList8* CommandList() const { return m_CommandBuffer->CommandList(); }
protected:
  CommandBuffer* m_CommandBuffer;
};

struct GlobalBarrierDesc {
  StageAccess from;
  StageAccess to;
};

struct BufferBarrierDesc {
  Buffer* resource;
  StageAccess from;
  StageAccess to;
};

struct TextureBarrierDesc {
  Texture* resource;
  StageAccessLayout from;
  StageAccessLayout to;
  // SubresourceRange range;
};

struct BarriersDesc {
  // std::span<GlobalBarrierDesc> globals{};
  std::span<BufferBarrierDesc> buffers{};
  std::span<TextureBarrierDesc> textures{};
};

class CommandEncoder : public EncoderBase
{
public:
  CommandEncoder(std::string label, CommandBuffer* commandBuffer);
  ~CommandEncoder();

  // TODO: make it so that we can't begin a pass if another one hasn't yet ended
  ComputePassEncoder BeginComputePass(const ComputePassDesc& desc);
  RenderPassEncoder BeginRenderPass(const RenderPassDesc& desc);
  MeshPassEncoder BeginMeshPass(const RenderPassDesc& desc);
  RayTracingPassEncoder BeginRayTracingPass(const RenderPassDesc& desc);

  // TODO: make these uncallable if a pass has begun+not yet ended?
  void Barrier(const BarriersDesc& desc);
  void CopyBufferToBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t size);

  // TODO: assert that every pass has ended
  CommandBuffer* Finish();
private:
  std::string m_Label;
};

struct ComputePassDesc {
  std::string label;
  TimestampWrites* timestampWrites;
};

class ComputePassEncoder : public EncoderBase
{
public:
  ComputePassEncoder(const ComputePassDesc& desc, CommandBuffer* commandBuffer);
  ~ComputePassEncoder();

  void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1);
  void End();
  void PushConstants(uint32_t offset, uint32_t size, const void *data);
  void SetPipeline(ComputePipeline* pipeline);
private:
  ComputePassDesc m_Desc;
  bool m_Ended = false;
};

struct Color {
  float r, g, b, a;
};

enum class LoadOp { Clear, Load, DontCare };
// enum class StoreOp { Store, Discard };

struct ColorAttachment {
  TextureView* view;
  uint32_t depthSlice = 0;
  TextureView* resolveTarget = nullptr;
  Color clearValue;
  LoadOp loadOp;
  // StoreOp storeOp;
};

struct DepthStencilAttachment {
  TextureView* view;
  float depthClearValue;
  LoadOp depthLoadOp;
  // StoreOp depthStoreOp;
  // bool depthReadOnly = false;
  uint32_t stencilClearValue = 0;
  LoadOp stencilLoadOp;
  // StoreOp stencilStoreOp;
  // bool stencilReadOnly = false;
};

struct RenderPassDesc {
  std::string label;
  std::span<ColorAttachment> colorAttachment;
  DepthStencilAttachment depthStencilAttachment;
  TimestampWrites* timestampWrites;
};

class RenderPassEncoder : public EncoderBase
{
public:
  RenderPassEncoder(const RenderPassDesc& desc, CommandBuffer* commandBuffer);
  ~RenderPassEncoder();

  void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
  void End();
  void PushConstants(uint32_t offset, uint32_t size, const void *data);
  void SetPipeline(RenderPipeline* pipeline);
private:
  RenderPassDesc m_Desc;
  bool m_Ended = false;
};

class MeshPassEncoder : public EncoderBase
{
public:
  MeshPassEncoder(const RenderPassDesc& desc, CommandBuffer* commandBuffer);
  ~MeshPassEncoder();

  void DrawMeshIndirect();
  void End();
  void PushConstants(uint32_t offset, uint32_t size, const void *data);
  void SetPipeline(MeshPipeline* pipeline);
private:
  RenderPassDesc m_Desc;
};

}  // namespace IssouRHI
