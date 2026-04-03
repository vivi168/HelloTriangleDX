#include "IssouRHI.h"

namespace IssouRHI
{
CommandEncoder::CommandEncoder(std::string label, CommandBuffer* commandBuffer) : EncoderBase(commandBuffer), m_Label(label) {}

CommandEncoder::~CommandEncoder()
{
  assert(m_CommandBuffer == nullptr); // Forgot to call Finish()
  // or instead of assert'ing simply close cmd list and recycle cmd buffer since it was not used?
}

ComputePassEncoder CommandEncoder::BeginComputePass(const ComputePassDesc& desc)
{
  m_CommandBuffer->SetComputeRootSignatureIfNeeded();

  // TODO: keep track of open state in CommandEncoder, so we can't call CopyBufferToBuffer/Barrier etc while recording a compute/render pass
  // or begincomputepass and immediately beginrenderpass, etc.
  // (force split pass)

  if (desc.timestampWrites != nullptr && desc.timestampWrites->beginningOfPassWriteIndex != QuerySetIndexUndefined) {
    // FIXME: validate that index doesn't exceed QuerySet#count
    // and also validate that querySet != nullptr and is QueryType::Timestamp
    // FIXME: delegate this to QuerySet
    CommandList()->EndQuery(desc.timestampWrites->querySet->QueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, desc.timestampWrites->beginningOfPassWriteIndex);
  }

  return ComputePassEncoder(desc, m_CommandBuffer);
}

RenderPassEncoder CommandEncoder::BeginRenderPass(const RenderPassDesc& desc)
{
  auto e = RenderPassEncoder(desc, m_CommandBuffer);

  m_CommandBuffer->SetComputeRootSignatureIfNeeded();

  // TODO ClearRenderTargetView, ClearDepthStencilView
  // OMSetRenderTargets
  // RSSetScissorRects, RSSetViewports
  // etc etc

  return e;
}

static D3D12_BARRIER_SYNC D3D12BarrierSync(PipelineStage stage)
{
  if (stage == PipelineStage::All) {
    return D3D12_BARRIER_SYNC_ALL;
  }

  if (stage == PipelineStage::None) {
    return D3D12_BARRIER_SYNC_NONE;
  }

  D3D12_BARRIER_SYNC flags = D3D12_BARRIER_SYNC_NONE;

  if (stage & PipelineStage::IndexInput) {
    flags |= D3D12_BARRIER_SYNC_INDEX_INPUT;
  }
  if (stage & (PipelineStage::VertexShader | PipelineStage::MeshShaders)) {
    flags |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
  }
  if (stage & PipelineStage::FragmentShader) {
    flags |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
  }
  if (stage & PipelineStage::DepthStencilAttachment) {
    flags |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
  }
  if (stage & PipelineStage::ColorAttachment) {
    flags |= D3D12_BARRIER_SYNC_RENDER_TARGET;
  }
  if (stage & PipelineStage::ComputeShader) {
    flags |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
  }
  if (stage & PipelineStage::RayTracingShaders) {
    flags |= D3D12_BARRIER_SYNC_RAYTRACING;
  }
  if (stage & (PipelineStage::AccelerationStructure | PipelineStage::Micromap)) {
    flags |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE | D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
    // D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO ?
  }
  if (stage & PipelineStage::Indirect) {
    flags |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
  }
  if (stage & PipelineStage::Copy) {
    flags |= D3D12_BARRIER_SYNC_COPY;
  }
  if (stage & PipelineStage::Resolve) {
    flags |= D3D12_BARRIER_SYNC_RESOLVE;
  }
  if (stage & PipelineStage::ClearStorage) {
    flags |= D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
  }

  return flags;
}

static D3D12_BARRIER_ACCESS D3D12BarrierAccess(Access access)
{
  if (access == Access::None) {
    return D3D12_BARRIER_ACCESS_NO_ACCESS;
  }

  D3D12_BARRIER_ACCESS flags = D3D12_BARRIER_ACCESS_COMMON;

  if (access & Access::IndexBuffer) {
    flags |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
  }
  if (access & Access::VertexBuffer) {
    flags |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
  }
  if (access & Access::ConstantBuffer) {
    flags |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
  }
  if (access & Access::ArgumentBuffer) {
    flags |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
  }
  if (access & (Access::ScratchBuffer | Access::ShaderResourceStorage | Access::ClearStorage)) {
    flags |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
  }
  if (access & (Access::ColorAttachmentRead | Access::ColorAttachmentWrite)) {
    flags |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
  }
  if (access & Access::DepthStencilAttachmentRead) {
    flags |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
  }
  if (access & Access::DepthStencilAttachmentWrite) {
    flags |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
  }
  if (access & Access::ShadingRateAttachment) {
    flags |= D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE;
  }
  if (access & (Access::InputAttachment | Access::ShaderResource | Access::ShaderBindingTable)) {
    flags |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
  }
  if (access & (Access::AccelerationStructureRead | Access::MicromapRead)) {
    flags |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ;
  }
  if (access & (Access::AccelerationStructureWrite | Access::MicromapWrite)) {
    flags |= D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;
  }
  if (access & Access::CopySource) {
    flags |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
  }
  if (access & Access::CopyDestination) {
    flags |= D3D12_BARRIER_ACCESS_COPY_DEST;
  }
  if (access & Access::ResolveSource) {
    flags |= D3D12_BARRIER_ACCESS_RESOLVE_SOURCE;
  }
  if (access & Access::ResolveDestination) {
    flags |= D3D12_BARRIER_ACCESS_RESOLVE_DEST;
  }

  return flags;
}

static D3D12_BARRIER_LAYOUT D3D12BarrierLayout(TextureLayout layout)
{
  switch (layout) {
    case TextureLayout::Undefined:
      return D3D12_BARRIER_LAYOUT_UNDEFINED;
    case TextureLayout::General:
      return D3D12_BARRIER_LAYOUT_COMMON;
    case TextureLayout::Present:
      return D3D12_BARRIER_LAYOUT_PRESENT;
    case TextureLayout::ColorAttachment:
      return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    case TextureLayout::DepthStencilAttachment:
    case TextureLayout::DepthReadonlyStencilAttachment:
    case TextureLayout::DepthAttachmentStencilReadonly:
      return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
    case TextureLayout::DepthStencilReadonly:
      return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
    case TextureLayout::ShadingRateAttachment:
      return D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE;
    case TextureLayout::InputAttachment:
      return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    case TextureLayout::ShaderResource:
      return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    case TextureLayout::ShaderResourceStorage:
      return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
    case TextureLayout::CopySource:
      return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    case TextureLayout::CopyDestination:
      return D3D12_BARRIER_LAYOUT_COPY_DEST;
    case TextureLayout::ResolveSource:
      return D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE;
    case TextureLayout::ResolveDestination:
      return D3D12_BARRIER_LAYOUT_RESOLVE_DEST;
    default:
      std::unreachable();
  }
}

static std::optional<D3D12_BUFFER_BARRIER> Transition(Buffer* buf, StageAccess from, StageAccess to)
{
  auto fromAccess = D3D12BarrierAccess(from.access);
  auto toAccess = D3D12BarrierAccess(to.access);
  bool accessChanged = from.access != to.access;
  bool storageBarrier = fromAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS && toAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

  if (!accessChanged && !storageBarrier)
    return std::nullopt;

  auto barrier = CD3DX12_BUFFER_BARRIER(D3D12BarrierSync(from.stage),
                                        D3D12BarrierSync(to.stage),
                                        fromAccess,
                                        toAccess,
                                        buf->Resource());

  return barrier;
}

static std::optional<D3D12_TEXTURE_BARRIER> Transition(Texture* tex, StageAccessLayout from, StageAccessLayout to)
{
  auto fromAccess = D3D12BarrierAccess(from.access);
  auto toAccess = D3D12BarrierAccess(to.access);
  auto fromLayout = D3D12BarrierLayout(from.layout);
  auto toLayout = D3D12BarrierLayout(to.layout);
  bool accessLayoutChanged = fromAccess != toAccess || fromLayout != toLayout;
  bool storageBarrier = fromAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS && toAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

  if (!accessLayoutChanged && !storageBarrier)
    return std::nullopt;

  auto barrier = CD3DX12_TEXTURE_BARRIER(D3D12BarrierSync(from.stage),
                                         D3D12BarrierSync(to.stage),
                                         fromAccess,
                                         toAccess,
                                         fromLayout,
                                         toLayout,
                                         tex->Resource(),
                                         CD3DX12_BARRIER_SUBRESOURCE_RANGE(0xffffffff),  // TODO
                                         D3D12_TEXTURE_BARRIER_FLAG_NONE);

  return barrier;
}

// FIXME: make the RHI dumb and leave barrier elision to the app?
void CommandEncoder::Barrier(const BarriersDesc& desc)
{
  std::vector<D3D12_BUFFER_BARRIER> bufferBarriers(desc.buffers.size());
  std::vector<D3D12_TEXTURE_BARRIER> textureBarriers(desc.textures.size());

  size_t nb = 0;
  for (auto& b : desc.buffers) {
    auto barrier = Transition(b.resource, b.from, b.to);
    if (barrier) {
      bufferBarriers[nb++] = barrier.value();
    }
  }

  size_t nt = 0;
  for (auto& t : desc.textures) {
    auto barrier = Transition(t.resource, t.from, t.to);
    if (barrier) {
      textureBarriers[nt++] = barrier.value();
    }
  }

  D3D12_BARRIER_GROUP barrierGroups[2]; // 3 with Global Barriers
  UINT n = 0;

  if (nb > 0) {
    barrierGroups[n++] = CD3DX12_BARRIER_GROUP(nb, bufferBarriers.data());
  }

  if (nt > 0) {
    barrierGroups[n++] = CD3DX12_BARRIER_GROUP(nt, textureBarriers.data());
  }

  if (n > 0) {
    CommandList()->Barrier(n, barrierGroups);
  }
}

void CommandEncoder::CopyBufferToBuffer(Buffer* src, size_t srcOffset, Buffer* dst, size_t dstOffset, size_t size)
{
  // TODO: validate input?
  CommandList()->CopyBufferRegion(dst->Resource(), dstOffset, src->Resource(), srcOffset, size);
}

CommandBuffer* CommandEncoder::Finish()
{
  m_CommandBuffer->CommandList()->Close();

  CommandBuffer* ptr = m_CommandBuffer;
  m_CommandBuffer = nullptr;

  return ptr;
}

EncoderBase::EncoderBase(CommandBuffer* commandBuffer) : m_CommandBuffer(commandBuffer) {}

EncoderBase::~EncoderBase() = default;

ComputePassEncoder::ComputePassEncoder(const ComputePassDesc& desc, CommandBuffer* commandBuffer) : EncoderBase(commandBuffer), m_Desc(desc) {}

ComputePassEncoder::~ComputePassEncoder()
{
  // assert closed
}

void ComputePassEncoder::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
  CommandList()->Dispatch(x, y, z);
}

void ComputePassEncoder::End()
{
  if (m_Desc.timestampWrites != nullptr && m_Desc.timestampWrites->endOfPassWriteIndex != QuerySetIndexUndefined) {
    // FIXME: validate that index doesn't exceed QuerySet#count
    // and also validate that querySet != nullptr and is QueryType::Timestamp
    // FIXME: delegate this to QuerySet
    CommandList()->EndQuery(m_Desc.timestampWrites->querySet->QueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, m_Desc.timestampWrites->endOfPassWriteIndex);
  }

  // TODO: mark encoder as open = false
}

void ComputePassEncoder::SetPipeline(ComputePipeline* pipeline)
{
  // FIXME: only if pso changed. track current pso in CommandBuffer
  CommandList()->SetPipelineState(pipeline->PipelineState());
}

RenderPassEncoder::RenderPassEncoder(const RenderPassDesc& desc, CommandBuffer* commandBuffer) : EncoderBase(commandBuffer), m_Desc(desc) {}

RenderPassEncoder::~RenderPassEncoder()
{
  // assert closed
}

void RenderPassEncoder::DrawInstanced()
{
  //
}

void RenderPassEncoder::End()
{
  // TODO: mark encoder as open = false
}

void RenderPassEncoder::SetPipeline(RenderPipeline* pipeline)
{
  // IASetPrimitiveTopology
}

MeshPassEncoder::MeshPassEncoder(const RenderPassDesc& desc, CommandBuffer* commandBuffer) : EncoderBase(commandBuffer), m_Desc(desc) {}

MeshPassEncoder::~MeshPassEncoder()
{
  // assert closed
}

void MeshPassEncoder::DrawMeshIndirect()
{
  //
}

void MeshPassEncoder::End()
{
  //
}

void MeshPassEncoder::SetPipeline(MeshPipeline* pipeline)
{
  //
}
}
