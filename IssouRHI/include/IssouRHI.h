#pragma once

// TODO: get rid of STL in public header?
// would need: string (=> const char* ?) span(=> ptr + size?), optional(=> nullptr ?), variant(=> enum+union ?), shared_ptr(=> ComPtr like class?)
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <cassert>
#include <cstdint>

#ifdef _DEBUG
#define ENABLE_DEBUG_LAYER true
#endif

#define ISSOURHI_BIT(b) (1 << (b))
#define ISSOURHI_ENUM_CLASS_OP(e)                                              \
  static_assert(sizeof(e) <= sizeof(uint32_t));                                \
  constexpr e operator~(e a) { return (e)(~(uint32_t)a); }                     \
  constexpr uint32_t operator&(e a, e b) { return (uint32_t)a & (uint32_t)b; } \
  constexpr e operator|(e a, e b) { return (e)((uint32_t)a | (uint32_t)b); }   \
  constexpr e& operator&=(e& a, e b)                                           \
  {                                                                            \
    a = (e)(a & b);                                                            \
    return a;                                                                  \
  }                                                                            \
  constexpr e& operator|=(e& a, e b)                                           \
  {                                                                            \
    a = (e)(a | b);                                                            \
    return a;                                                                  \
  }

namespace IssouRHI
{
struct GPUSelection {
  uint32_t Index = UINT32_MAX;
  std::wstring Substring;  // FIXME: make this not wstring
};

// TODO: Utils
inline bool IsPowerOfTwo(size_t v)
{
  return (v != 0) && ((v & (v - 1)) == 0);
}

inline size_t AlignUpPowerOfTwo(size_t size, size_t align)
{
  assert(IsPowerOfTwo(align));
  return (size + align - 1) & ~(align - 1);
}

class Device;

enum class QueryType {
  Timestamp,
  // Occlusion?
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
  virtual ~QuerySet();

  virtual void Create() = 0;

  QueryType Type() const { return m_Desc.type; }

  uint32_t Count() const { return m_Desc.count; }

protected:
  Device* m_Device;
  QuerySetDesc m_Desc;
};

struct TimestampWrites {
  uint32_t beginningOfPassWriteIndex;
  uint32_t endOfPassWriteIndex;

  QuerySet* querySet;
};

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
ISSOURHI_ENUM_CLASS_OP(PipelineStage)

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
ISSOURHI_ENUM_CLASS_OP(Access)

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
  CopySrc = ISSOURHI_BIT(0),
  CopyDst = ISSOURHI_BIT(1),
  TextureBinding = ISSOURHI_BIT(2),
  StorageBinding = ISSOURHI_BIT(3),
  RenderAttachment = ISSOURHI_BIT(4),
};
ISSOURHI_ENUM_CLASS_OP(TextureUsage)

enum class TextureAspect : uint32_t {
  All,
  DepthOnly,
  StencilOnly,
};

inline uint32_t PlaneSlice(TextureAspect aspect)
{
  switch (aspect) {
    case TextureAspect::All:
    case TextureAspect::DepthOnly:
      return 0;
    case TextureAspect::StencilOnly:
      // TODO
      return 1;
  }
}

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

inline bool IsDepthStencil(TextureFormat format)
{
  switch (format) {
    case TextureFormat::Depth32Float:
      return true;
    default:
      return false;
  }
}

enum class VertexFormat : uint32_t {
  Undefined,
  Float32x3,
};

enum class IndexFormat : uint32_t {
  Undefined,
  Uint16,
  Uint32,
};

struct Extent3D {
  uint32_t width;
  uint32_t height = 1;
  uint32_t depth = 1;

  bool operator==(const Extent3D& other) const
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

  bool operator==(const SubresourceRange& other) const
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

  bool operator==(const TextureViewDesc& other) const
  {
    return format == other.format &&
           dimension == other.dimension &&
           aspect == other.aspect &&
           range == other.range;
  }

  struct Hasher {
    size_t operator()(const TextureViewDesc& desc) const
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

struct TextureSubresource {
  uint64_t rowPitch;
  uint64_t slicePitch;
  const void* data;
};

class Texture
{
public:
  Texture(Device* device, const TextureDesc& desc);
  virtual ~Texture();

  virtual void Create() = 0;

  virtual void Write(std::span<TextureSubresource> subresources, uint32_t baseMipLevel = 0, uint32_t baseArrayLayer = 0) = 0;

  // use std::expected (C++23) ?
  std::shared_ptr<TextureView> CreateView();
  virtual std::shared_ptr<TextureView> CreateView(const TextureViewDesc& desc) = 0;

  Device* GetDevice() const { return m_Device; }

  TextureUsage Usage() const { return m_Desc.usage; }

  TextureFormat Format() const { return m_Desc.format; }

  Extent3D Size() const { return m_Desc.size; };

  Extent3D SizeAtMipLevel(uint32_t level) const;

protected:
  bool IsMultiSampled() const { return m_Desc.sampleCount > 1; }

  std::unordered_map<TextureViewDesc, std::shared_ptr<TextureView>, TextureViewDesc::Hasher> m_Views;

  Device* m_Device;
  TextureDesc m_Desc;
};

enum class TextureAccess {
  Read,
  ReadWrite,
};

class TextureView
{
public:
  TextureView(Texture* tex, const TextureViewDesc& desc);
  virtual ~TextureView();

  virtual uint32_t DescriptorIndex(TextureAccess access) const = 0;
  virtual uint64_t DescriptorHandle(TextureAccess access) const = 0;

  Extent3D Size() const { return m_Texture->SizeAtMipLevel(m_Desc.range.baseMipLevel); }

protected:
  Texture* m_Texture;  // should it be owning? weak ref?
  TextureViewDesc m_Desc;
};

enum class BufferUsage : uint32_t {
  None = 0,
  MapRead = ISSOURHI_BIT(0),
  MapWrite = ISSOURHI_BIT(1),
  CopySrc = ISSOURHI_BIT(2),
  CopyDst = ISSOURHI_BIT(3),
  Index = ISSOURHI_BIT(4),
  Vertex = ISSOURHI_BIT(5),
  Uniform = ISSOURHI_BIT(6),
  Storage = ISSOURHI_BIT(7),
  Indirect = ISSOURHI_BIT(8),
  QueryResolve = ISSOURHI_BIT(9),
  RayTracingAccelerationStructure = ISSOURHI_BIT(10),
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

enum class BufferAccess {
  Constant,
  Read,
  ReadWrite,
};

class Buffer;

struct BufferViewDesc {
  BufferAccess access;
  BufferRange range = FullBufferRange;
  uint32_t elementStride = 0;
  size_t counterOffset = 0;
  Buffer* counter = nullptr;
};

class Buffer
{
public:
  Buffer(Device* device, const BufferDesc& desc);
  virtual ~Buffer();

  virtual void Create() = 0;

  uint64_t Size() const { return m_Desc.size; }

  virtual uint32_t DescriptorIndex(const BufferViewDesc& desc) = 0;
  virtual uint64_t GpuAddress() const = 0;

  virtual void Write(BufferRange range, const void* data) = 0;
  virtual void Clear(BufferRange range) = 0;
  virtual void Read(BufferRange range, void* outData) = 0;

protected:
  BufferRange ClampBufferRange(BufferRange range);

  Device* m_Device;
  BufferDesc m_Desc;
};

struct BufferWithOffset {
  Buffer* buffer = nullptr;
  uint64_t offset = 0;

  explicit operator bool() const { return buffer != nullptr; }

  uint64_t GpuAddress() const
  {
    return buffer ? buffer->GpuAddress() + offset : 0;
  }
};

enum class TopLevelInstanceFlags : uint32_t {
  None = 0,
  TriangleCullDisable = ISSOURHI_BIT(0),
  TriangleFrontCounterclockwise = ISSOURHI_BIT(1),
  ForceOpaque = ISSOURHI_BIT(2),
  ForceNonOpaque = ISSOURHI_BIT(3),
  ForceOMM2State = ISSOURHI_BIT(4),
  DisableOMMs = ISSOURHI_BIT(5)
};
ISSOURHI_ENUM_CLASS_OP(TopLevelInstanceFlags)

struct TopLevelInstanceDesc {
  float transformMatrix[3][4];
  uint32_t instanceId : 24;
  uint32_t instanceMask : 8;
  uint32_t instanceContributionToHitGroupIndex : 24;
  TopLevelInstanceFlags flags : 8;
  uint64_t accelerationStructureGpuAddress;
};

enum class BottomLevelGeometryFlags : uint32_t {
  None = 0,
  Opaque = ISSOURHI_BIT(0),
  NoDuplicateAnyHitInvocation = ISSOURHI_BIT(1),
};
ISSOURHI_ENUM_CLASS_OP(BottomLevelGeometryFlags)

struct BottomLevelTrianglesDesc {
  BufferWithOffset transformMatrices{};  // TransformMatrix

  BufferWithOffset vertices;
  uint64_t vertexStride;
  uint32_t vertexCount;
  VertexFormat vertexFormat;

  BufferWithOffset indices{};
  uint32_t indexCount = 0;
  IndexFormat indexFormat = IndexFormat::Undefined;

  // omm
};

struct BottomLevelAABB {
  float minX;
  float minY;
  float minZ;
  float maxX;
  float maxY;
  float maxZ;
};

struct BottomLevelAABBsDesc {
  BufferWithOffset aabbs;  // BottomLevelAABB
  uint64_t stride;
  uint64_t count;
};

struct BottomLevelGeometryDesc {
  BottomLevelGeometryFlags flags;
  std::variant<BottomLevelTrianglesDesc, BottomLevelAABBsDesc> geometry;
};

enum class AccelerationStructureFlags : uint32_t {
  None = 0,
  AllowUpdate = ISSOURHI_BIT(0),
  AllowCompaction = ISSOURHI_BIT(1),
  AllowDataAccess = ISSOURHI_BIT(2),
  PreferFastTrace = ISSOURHI_BIT(3),
  PreferFastBuild = ISSOURHI_BIT(4),
  MinimizeMemory = ISSOURHI_BIT(5),
  AllowMicromapUpdate = ISSOURHI_BIT(6),
  AllowDisableMicromaps = ISSOURHI_BIT(7),
};
ISSOURHI_ENUM_CLASS_OP(AccelerationStructureFlags)

struct TopLevelDesc {
  std::span<TopLevelInstanceDesc> instances{};
};

struct BottomLevelDesc {
  std::span<BottomLevelGeometryDesc> geometries{};
};

struct AccelerationStructureDesc {
  std::string label;
  AccelerationStructureFlags flags;
  std::variant<TopLevelDesc, BottomLevelDesc> geometryOrInstanceDesc;
};

class AccelerationStructure
{
public:
  AccelerationStructure(Device* device);
  virtual ~AccelerationStructure();

  virtual void Create(const AccelerationStructureDesc& desc) = 0;

  virtual uint32_t DescriptorIndex() const = 0;

  uint64_t GpuAddress() const { return m_Buffer->GpuAddress(); }

  uint64_t ScratchGpuAddress() const { return m_ScratchBuffer->GpuAddress(); }

protected:
  Device* m_Device;
  std::shared_ptr<Buffer> m_Buffer;
  std::shared_ptr<Buffer> m_ScratchBuffer;
};

enum class ShaderStage {
  Compute,
  Fragment,
  Vertex,
  Mesh,
  Task,
  // RayTracing
  RayGen,
  RayMiss,
  RayIntersection,
  RayClosestHit,
  RayAnyHit,
  Callable,
};

struct ShaderModule {
  ShaderStage stage;
  const void* code;
  size_t size;
  std::optional<std::string> entryPointName = std::nullopt;
};

struct ComputePipelineDesc {
  std::string label;
  ShaderModule shader;
};

class ComputePipeline
{
public:
  ComputePipeline(Device* device);
  virtual ~ComputePipeline();

  virtual void Create(const ComputePipelineDesc& desc) = 0;

protected:
    Device* m_Device;
};

enum ColorWriteFlags : uint8_t {
  Red = ISSOURHI_BIT(0),
  Green = ISSOURHI_BIT(1),
  Blue = ISSOURHI_BIT(2),
  Alpha = ISSOURHI_BIT(3),
  All = Red | Green | Blue | Alpha,
};
ISSOURHI_ENUM_CLASS_OP(ColorWriteFlags)

enum class BlendOperation {
  Add,
  Subtract,
  ReverseSubtract,
  Min,
  Max,
};

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
  std::optional<BlendState> blend = std::nullopt;
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
};

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
  StencilFaceState stencilFront{};
  StencilFaceState stencilBack{};
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

enum class FrontFace {
  CCW,
  CW,
};

enum class CullMode {
  None,
  Front,
  Back,
};

struct PrimitiveState {
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  IndexFormat stripIndexFormat = IndexFormat::Undefined;
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
  std::span<ShaderModule> shaders;
  std::span<ColorTargetState> targets;
  DepthStencilState depthStencil{};
  PrimitiveState primitive{};
  MultisampleState multiSample{};
};

class RenderPipeline
{
public:
  enum class Type {
    Render,
    Mesh,
  };

  RenderPipeline(Device* device, Type type);

  virtual ~RenderPipeline();

  virtual void Create(const RenderPipelineDesc& desc) = 0;

protected:
  Device* m_Device;
  Type m_Type;
};

enum class RayTracingPipelineFlags : uint32_t {
  None = 0,
  SkipTriangles = ISSOURHI_BIT(0),
  SkipAABBs = ISSOURHI_BIT(1),
  AllowMicroMaps = ISSOURHI_BIT(2),
};
ISSOURHI_ENUM_CLASS_OP(RayTracingPipelineFlags)

struct HitGroupDesc {
  std::string name;
  std::optional<std::string> anyHitEntryPoint = std::nullopt;
  std::optional<std::string> closestHitEntryPoint = std::nullopt;
  std::optional<std::string> intersectionEntryPoint = std::nullopt;
};

struct RayTracingPipelineDesc {
  std::string label;
  std::span<ShaderModule> shaders;
  std::span<HitGroupDesc> hitGroups;
  uint32_t maxAttributeSize;
  uint32_t maxPayloadSize;
  uint32_t maxRecursionDepth;
  RayTracingPipelineFlags flags = RayTracingPipelineFlags::None;
  // TODO: omm
};

class RayTracingPipeline
{
public:
  RayTracingPipeline(Device* device);
  virtual ~RayTracingPipeline();

  virtual void Create(const RayTracingPipelineDesc& desc) = 0;

protected:
  Device* m_Device;
};

struct ShaderTableDesc {
  std::string label;
  const RayTracingPipeline* pipeline;
  std::string rayGenEntryPoint;
  std::span<std::string> missEntryPoints{};
  std::span<std::string> hitGroupNames{};
  std::span<std::string> callableEntryPoints{};
};

class ShaderTable
{
public:
  ShaderTable(Device* device);
  virtual ~ShaderTable();

  virtual void Create(const ShaderTableDesc& desc) = 0;

  protected:
  Device* m_Device;

  std::shared_ptr<Buffer> m_Buffer;

  uint32_t m_RayGenShaderRecordSize = 0;

  uint32_t m_MissShaderTableSize = 0;
  uint32_t m_MissShaderTableOffset = 0;

  uint32_t m_HitGroupTableSize = 0;
  uint32_t m_HitGroupTableOffset = 0;

  uint32_t m_CallableShaderTableSize = 0;
  uint32_t m_CallableShaderTableOffset = 0;
};

class Queue;
struct SurfaceConfiguration;
class Surface;

enum class Backend {
    D3D12,
    Metal,
    Vulkan,
};

class Device
{
public:
  static std::unique_ptr<Device> CreateDevice(Backend backend, const GPUSelection& gpuSelection);

  virtual ~Device();

  virtual void Create(const GPUSelection& gpuSelection) = 0;

  virtual void PrintAdapterInformation() = 0;

  Queue* GetQueue() const { return m_Queue.get(); }

  virtual std::shared_ptr<Surface> CreateSurface(void* handle) = 0;
  virtual std::shared_ptr<QuerySet> CreateQuerySet(const QuerySetDesc& desc) = 0;
  virtual std::shared_ptr<Texture> CreateTexture(const TextureDesc& desc) = 0;
  virtual std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc) = 0;
  virtual std::shared_ptr<AccelerationStructure> CreateAccelerationStructure(const AccelerationStructureDesc& desc) = 0;

  virtual std::shared_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
  virtual std::shared_ptr<RenderPipeline> CreateRenderPipeline(const RenderPipelineDesc& desc) = 0;
  virtual std::shared_ptr<RenderPipeline> CreateMeshPipeline(const RenderPipelineDesc& desc) = 0;
  virtual std::shared_ptr<RayTracingPipeline> CreateRayTracingPipelinePipeline(const RayTracingPipelineDesc& desc) = 0;
  virtual std::shared_ptr<ShaderTable> CreateShaderTable(const ShaderTableDesc& desc) = 0;

  uint64_t TimestampFrequencyHz() const { return m_TimestampFrequencyHz; }

protected:
  std::unique_ptr<Queue> m_Queue;
  uint64_t m_TimestampFrequencyHz = 1;
};

struct SurfaceConfiguration {
  TextureFormat format;
  uint32_t width;
  uint32_t height;
  uint32_t bufferCount;
  bool enableVsync = false;
};

class Surface
{
public:
  Surface(Device* device, void* handle);
  virtual ~Surface();

  virtual void Create() = 0;

  void Configure(SurfaceConfiguration& config);
  virtual std::shared_ptr<Texture> GetCurrentTexture() = 0;
  virtual void Present() = 0;

  uint32_t CurrentFrameIndex() const { return m_FrameIndex; }

protected:
  virtual void CreateSwapChain(SurfaceConfiguration& config) = 0;
  virtual void CreateTextures(SurfaceConfiguration& config) = 0;

  Device* m_Device;
  void* m_Handle;

  std::vector<std::shared_ptr<Texture>> m_Textures;

  uint32_t m_FrameIndex;
  bool m_EnableVsync = false;
  bool m_Configured = false;
};

class CommandEncoder;

class CommandBuffer
{
public:
  CommandBuffer(Device* device);
  virtual ~CommandBuffer();

  virtual void Create() = 0;
  virtual void Init() = 0;
  virtual void Reset() = 0;

  Device* GetDevice() const { return m_Device; }

  uint64_t FenceValue() const { return m_FenceValue; }

  void UpdateFenceValue(uint64_t value) { m_FenceValue = value; }

protected:
  Device* m_Device;
  uint64_t m_FenceValue = 0;
};

class Queue
{
public:
  Queue(Device* device);
  virtual ~Queue();

  virtual void Create() = 0;

  std::unique_ptr<CommandEncoder> CreateCommandEncoder(std::optional<std::string> label = std::nullopt);

  virtual void Submit(std::span<CommandBuffer*> commandBuffers) = 0;
  virtual void WaitForAll() = 0;

protected:
  CommandBuffer* FindOrCreateCommandBuffer();
  CommandBuffer* CreateCommandBuffer();
  void RecycleCommandBuffers();
  void ResetCommandBuffer(CommandBuffer* commandBuffer);

  virtual std::unique_ptr<CommandEncoder> CreateCommandEncoderImpl(std::string label, CommandBuffer* commandBuffer) = 0;
  virtual std::unique_ptr<CommandBuffer> CreateCommandBufferImpl() = 0;

  virtual uint64_t FenceCompletedValue() const = 0;

  Device* m_Device;

  // TODO: mutex to protect these
  std::vector<std::unique_ptr<CommandBuffer>> m_CommandBuffers;
  std::list<CommandBuffer*> m_CommandBuffersExecuting;
  std::list<CommandBuffer*> m_CommandBuffersAvailable;
};

class ComputePassEncoder;
class RenderPassEncoder;
class RayTracingPassEncoder;

struct ComputePassDesc;
struct RenderPassDesc;
struct RayTracingPassDesc;

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
  // TODO: SubresourceRange range;
};

struct BarriersDesc {
  std::span<GlobalBarrierDesc> globals{};
  std::span<BufferBarrierDesc> buffers{};
  std::span<TextureBarrierDesc> textures{};
};

class CommandEncoder
{
public:
  CommandEncoder(std::string label, CommandBuffer* commandBuffer);
  virtual ~CommandEncoder();

  // TODO: make it so that we can't begin a pass if another one hasn't yet ended
  virtual std::unique_ptr<ComputePassEncoder> BeginComputePass(const ComputePassDesc& desc) = 0;
  virtual std::unique_ptr<RenderPassEncoder> BeginRenderPass(const RenderPassDesc& desc) = 0;
  virtual std::unique_ptr<RayTracingPassEncoder> BeginRayTracingPass(const RayTracingPassDesc& desc) = 0;

  // TODO: make these uncallable if a pass has begun+not yet ended?
  virtual void Barrier(const BarriersDesc& desc) = 0;
  virtual void BuildTopLevelAccelerationStructure(AccelerationStructure* dst, BufferWithOffset instances, uint32_t instanceCount, AccelerationStructure* src = nullptr) = 0;
  virtual void BuildBottomLevelAccelerationStructure(AccelerationStructure* dst, std::span<BottomLevelGeometryDesc> geometries, AccelerationStructure* src = nullptr) = 0;
  virtual void CopyBufferToBuffer(Buffer* src, uint64_t srcOffset, Buffer* dst, uint64_t dstOffset, uint64_t size) = 0;
  virtual void ResolveQuerySet(QuerySet* querySet, uint32_t firstQuery, uint32_t queryCount, Buffer* dst, uint64_t dstOffset) = 0;
  virtual void WriteTimestamp(QuerySet* querySet, uint32_t index) = 0;

  // TODO: BuildOpacityMicroMaps

  // TODO: assert that every pass has ended
  virtual CommandBuffer* Finish() = 0;

  Device* GetDevice() const { return m_CommandBuffer->GetDevice(); }

protected:
  std::string m_Label;
  CommandBuffer* m_CommandBuffer;
};

struct ComputePassDesc {
  std::string label;
  std::optional<TimestampWrites> timestampWrites = std::nullopt;
};

class ComputePassEncoder
{
public:
  ComputePassEncoder(const ComputePassDesc& desc, CommandBuffer* commandBuffer);
  virtual ~ComputePassEncoder();

  virtual void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1) = 0;
  // TODO: virtual DispatchIndirect(indirectBuffer, indirectOffset)
  virtual void End() = 0;
  virtual void PushConstants(uint32_t offset, uint32_t size, const void* data) = 0;
  virtual void SetPipeline(ComputePipeline* pipeline) = 0;

protected:
  CommandBuffer* m_CommandBuffer;
  ComputePassDesc m_Desc;  // FIXME: remove this and store TimestampWrites instead
  bool m_Ended = false;
};

struct Color {
  float r, g, b, a;
};

enum class LoadOp {
  Clear,
  Load,
  DontCare,
};

// enum class StoreOp { Store, Discard };

struct ColorAttachment {
  TextureView* view;
  uint32_t depthSlice = 0;
  TextureView* resolveTarget = nullptr;
  Color clearValue;
  LoadOp loadOp = LoadOp::Clear;
  // StoreOp storeOp;
};

struct DepthStencilAttachment {
  TextureView* view = nullptr;
  float depthClearValue;
  LoadOp depthLoadOp = LoadOp::Clear;
  // StoreOp depthStoreOp;
  // bool depthReadOnly = false;
  uint32_t stencilClearValue = 0;
  LoadOp stencilLoadOp = LoadOp::Clear;
  // StoreOp stencilStoreOp;
  // bool stencilReadOnly = false;
};

struct RenderPassDesc {
  std::string label;
  std::span<ColorAttachment> colorAttachment;
  DepthStencilAttachment depthStencilAttachment{};
  std::optional<TimestampWrites> timestampWrites = std::nullopt;
};

class RenderPassEncoder
{
public:
  RenderPassEncoder(const RenderPassDesc& desc, CommandBuffer* commandBuffer);
  virtual ~RenderPassEncoder();

  virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
  // TODO: DrawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
  // TODO: DrawIndirect(indirectBuffer, indirectOffset)
  // TODO: DrawIndexedIndirect(indirectBuffer, indirectOffset)
  // TODO: DrawMesh();
  virtual void DrawMeshIndirect(Buffer* indirectBuffer, uint64_t indirectOffset, uint32_t maxDrawCount, Buffer* countBuffer = nullptr, uint64_t countOffset = 0) = 0;
  virtual void End() = 0;
  virtual void PushConstants(uint32_t offset, uint32_t size, const void* data) = 0;
  virtual void SetPipeline(RenderPipeline* pipeline) = 0;

protected:
  CommandBuffer* m_CommandBuffer;
  RenderPassDesc m_Desc;  // FIXME: remove this and store TimestampWrites instead.
  bool m_Ended = false;
};

// TODO: DRY somehow with compute pass ?
struct RayTracingPassDesc {
  std::string label;
  std::optional<TimestampWrites> timestampWrites = std::nullopt;
};

class RayTracingPassEncoder
{
public:
  RayTracingPassEncoder(const RayTracingPassDesc& desc, CommandBuffer* commandBuffer);
  virtual ~RayTracingPassEncoder();

  virtual void End() = 0;
  virtual void PushConstants(uint32_t offset, uint32_t size, const void* data) = 0;
  virtual void SetPipeline(RayTracingPipeline* pipeline) = 0;
  virtual void TraceRays(ShaderTable* shaderTable, uint32_t width, uint32_t height, uint32_t depth = 1) = 0;
  // TODO: TraceRaysIndirect

protected:
  CommandBuffer* m_CommandBuffer;
  RayTracingPassDesc m_Desc;
  bool m_Ended = false;
};

}  // namespace IssouRHI
