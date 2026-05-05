#include "CommandEncoderD3D12.h"

#include "AccelerationStructureD3D12.h"
#include "BufferD3D12.h"
#include "DeviceD3D12.h"
#include "PipelineD3D12.h"
#include "QuerySetD3D12.h"
#include "ShaderTableD3D12.h"
#include "TextureD3D12.h"
#include "UtilsD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
CommandEncoderImpl::CommandEncoderImpl(std::string label, CommandBuffer* commandBuffer) : CommandEncoder(label, commandBuffer) {}

CommandEncoderImpl::~CommandEncoderImpl() = default;

static D3D12_QUERY_TYPE D3D12QueryType(QueryType type)
{
  switch (type) {
    case QueryType::Timestamp:
      return D3D12_QUERY_TYPE_TIMESTAMP;
    default:
      std::unreachable();
  };
}

static void D3D12WriteTimestamp(ID3D12GraphicsCommandList8* commandList, QuerySet* querySet, uint32_t index)
{
  assert(querySet != nullptr);
  assert(querySet->Type() == QueryType::Timestamp);
  assert(index < querySet->Count());
  commandList->EndQuery(ToBackend(querySet)->QueryHeap(), D3D12QueryType(querySet->Type()), index);
}

std::unique_ptr<ComputePassEncoder> CommandEncoderImpl::BeginComputePass(const ComputePassDesc& desc)
{
  ToBackend(m_CommandBuffer)->SetComputeRootSignatureIfNeeded();

  // TODO: keep track of open state in CommandEncoder, so we can't call CopyBufferToBuffer/Barrier etc while recording a compute/render pass
  // or begincomputepass and immediately beginrenderpass, etc.
  // (force split pass)
  if (desc.timestampWrites.has_value()) {
    D3D12WriteTimestamp(CommandList(), desc.timestampWrites->querySet, desc.timestampWrites->beginningOfPassWriteIndex);
  }

  return std::make_unique<ComputePassEncoderImpl>(desc, m_CommandBuffer);
}

std::unique_ptr<RenderPassEncoder> CommandEncoderImpl::BeginRenderPass(const RenderPassDesc& desc)
{
  ToBackend(m_CommandBuffer)->SetGraphicsRootSignatureIfNeeded();

  if (desc.timestampWrites.has_value()) {
    D3D12WriteTimestamp(CommandList(), desc.timestampWrites->querySet, desc.timestampWrites->beginningOfPassWriteIndex);
  }

  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
  bool hasReference = false;
  Extent3D referenceSize;
  // FIXME: also validate that sampleCount matches between targets

  for (const auto& colorAttachment : desc.colorAttachment) {
    if (colorAttachment.view != nullptr) {
      auto rtvHandle = ToBackend(colorAttachment.view)->RtvDescriptorAlloc().cpuHandle;
      rtvHandles.push_back(rtvHandle);

      if (!hasReference) {
        referenceSize = colorAttachment.view->Size();
        hasReference = true;
      } else {
        assert(colorAttachment.view->Size() == referenceSize);
      }

      if (colorAttachment.loadOp == LoadOp::Clear) {
        const float clearColor[4] = {
            colorAttachment.clearValue.r,
            colorAttachment.clearValue.g,
            colorAttachment.clearValue.b,
            colorAttachment.clearValue.a,
        };

        CommandList()->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
      }
    }
  }

  bool hasColorAttachment = rtvHandles.size() > 0;
  bool hasDepthStencilAttachment = desc.depthStencilAttachment.view != nullptr;

  if (hasDepthStencilAttachment) {
    dsvHandle = ToBackend(desc.depthStencilAttachment.view)->DsvDescriptorAlloc().cpuHandle;

    if (!hasReference) {
      referenceSize = desc.depthStencilAttachment.view->Size();
      hasReference = true;
    } else {
      assert(desc.depthStencilAttachment.view->Size() == referenceSize);
    }

    D3D12_CLEAR_FLAGS clearFlags{};
    if (desc.depthStencilAttachment.depthLoadOp == LoadOp::Clear) {
      clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
    }
    if (desc.depthStencilAttachment.stencilLoadOp == LoadOp::Clear) {
      clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
    }
    assert(desc.depthStencilAttachment.depthClearValue >= 0.f && desc.depthStencilAttachment.depthClearValue <= 1.f);

    CommandList()->ClearDepthStencilView(dsvHandle,
                                         clearFlags,
                                         desc.depthStencilAttachment.depthClearValue,
                                         static_cast<UINT>(desc.depthStencilAttachment.stencilClearValue),
                                         0, nullptr);
  }

  CommandList()->OMSetRenderTargets(rtvHandles.size(), rtvHandles.data(), FALSE, hasDepthStencilAttachment ? &dsvHandle : nullptr);

  if (hasReference) {
    D3D12_VIEWPORT viewport{0.f, 0.f, static_cast<float>(referenceSize.width), static_cast<float>(referenceSize.height), 0.f, 1.f};
    CommandList()->RSSetViewports(1, &viewport);

    D3D12_RECT scissorRect{0, 0, static_cast<LONG>(referenceSize.width), static_cast<LONG>(referenceSize.height)};
    CommandList()->RSSetScissorRects(1, &scissorRect);
  }

  return std::make_unique<RenderPassEncoderImpl>(desc, m_CommandBuffer);
}

std::unique_ptr<RayTracingPassEncoder> CommandEncoderImpl::BeginRayTracingPass(const RayTracingPassDesc& desc)
{
  ToBackend(m_CommandBuffer)->SetComputeRootSignatureIfNeeded();

  if (desc.timestampWrites.has_value()) {
    D3D12WriteTimestamp(CommandList(), desc.timestampWrites->querySet, desc.timestampWrites->beginningOfPassWriteIndex);
  }

  return std::make_unique<RayTracingPassEncoderImpl>(desc, m_CommandBuffer);
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
    // FIXME: what about D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO ?
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

static std::optional<D3D12_GLOBAL_BARRIER> Transition(StageAccess from, StageAccess to)
{
  auto fromAccess = D3D12BarrierAccess(from.access);
  auto toAccess = D3D12BarrierAccess(to.access);

  bool accessChanged = from.access != to.access;
  bool storageBarrier = fromAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS && toAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

  if (!accessChanged && !storageBarrier) {
    return std::nullopt;
  }

  return D3D12_GLOBAL_BARRIER{
      .SyncBefore = D3D12BarrierSync(from.stage),
      .SyncAfter = D3D12BarrierSync(to.stage),
      .AccessBefore = fromAccess,
      .AccessAfter = toAccess,
  };
}

static std::optional<D3D12_BUFFER_BARRIER> Transition(Buffer* buf, StageAccess from, StageAccess to)
{
  auto fromAccess = D3D12BarrierAccess(from.access);
  auto toAccess = D3D12BarrierAccess(to.access);
  bool accessChanged = from.access != to.access;
  bool storageBarrier = fromAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS && toAccess == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

  if (!accessChanged && !storageBarrier) {
    return std::nullopt;
  }

  auto barrier = CD3DX12_BUFFER_BARRIER(D3D12BarrierSync(from.stage),
                                        D3D12BarrierSync(to.stage),
                                        fromAccess,
                                        toAccess,
                                        ToBackend(buf)->Resource());

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

  if (!accessLayoutChanged && !storageBarrier) {
    return std::nullopt;
  }

  auto barrier = CD3DX12_TEXTURE_BARRIER(D3D12BarrierSync(from.stage),
                                         D3D12BarrierSync(to.stage),
                                         fromAccess,
                                         toAccess,
                                         fromLayout,
                                         toLayout,
                                         ToBackend(tex)->Resource(),
                                         CD3DX12_BARRIER_SUBRESOURCE_RANGE(0xffffffff),  // TODO
                                         D3D12_TEXTURE_BARRIER_FLAG_NONE);

  return barrier;
}

// FIXME: make the RHI dumb and leave barrier elision to the app?
void CommandEncoderImpl::Barrier(const BarriersDesc& desc)
{
  // FIXME: use a small vector or something instead of doing heap allocation here?
  std::vector<D3D12_GLOBAL_BARRIER> globalBarriers(desc.globals.size());
  std::vector<D3D12_BUFFER_BARRIER> bufferBarriers(desc.buffers.size());
  std::vector<D3D12_TEXTURE_BARRIER> textureBarriers(desc.textures.size());

  UINT ng = 0;
  for (const auto& g : desc.globals) {
    auto barrier = Transition(g.from, g.to);
    if (barrier) {
      globalBarriers[ng++] = barrier.value();
    }
  }

  UINT nb = 0;
  for (const auto& b : desc.buffers) {
    auto barrier = Transition(b.resource, b.from, b.to);
    if (barrier) {
      bufferBarriers[nb++] = barrier.value();
    }
  }

  UINT nt = 0;
  for (const auto& t : desc.textures) {
    auto barrier = Transition(t.resource, t.from, t.to);
    if (barrier) {
      textureBarriers[nt++] = barrier.value();
    }
  }

  D3D12_BARRIER_GROUP barrierGroups[3];
  UINT n = 0;

  if (ng > 0) {
    barrierGroups[n++] = CD3DX12_BARRIER_GROUP(ng, globalBarriers.data());
  }

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

void CommandEncoderImpl::BuildTopLevelAccelerationStructure(AccelerationStructure* dst, BufferWithOffset instances, uint32_t instanceCount, AccelerationStructure* src)
{
  static_assert(sizeof(D3D12_RAYTRACING_INSTANCE_DESC) == sizeof(TopLevelInstanceDesc));

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
  desc.DestAccelerationStructureData = dst->GpuAddress();
  desc.ScratchAccelerationStructureData = dst->ScratchGpuAddress();
  desc.Inputs = {
      .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
      .Flags = ToBackend(dst)->Flags(),
      .NumDescs = instanceCount,
      .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
      .InstanceDescs = instances.GpuAddress(),
  };

  if (src != nullptr) {
    desc.SourceAccelerationStructureData = src->GpuAddress();
    desc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
  }

  CommandList()->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
}

void CommandEncoderImpl::BuildBottomLevelAccelerationStructure(AccelerationStructure* dst, std::span<BottomLevelGeometryDesc> geometries, AccelerationStructure* src)
{
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
  desc.DestAccelerationStructureData = dst->GpuAddress();
  desc.ScratchAccelerationStructureData = dst->ScratchGpuAddress();

  auto geometryDescs = D3D12RaytracingGeometryDescs(geometries);
  desc.Inputs = {
      .Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
      .Flags = ToBackend(dst)->Flags(),
      .NumDescs = static_cast<UINT>(geometryDescs.size()),
      .DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
      .pGeometryDescs = geometryDescs.data(),
  };

  if (src != nullptr) {
    desc.SourceAccelerationStructureData = src->GpuAddress();
    desc.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
  }

  CommandList()->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
}

void CommandEncoderImpl::CopyBufferToBuffer(Buffer* src, uint64_t srcOffset, Buffer* dst, uint64_t dstOffset, uint64_t size)
{
  // TODO: validate input?
  CommandList()->CopyBufferRegion(ToBackend(dst)->Resource(), dstOffset, ToBackend(src)->Resource(), srcOffset, size);
}

void CommandEncoderImpl::ResolveQuerySet(QuerySet* querySet, uint32_t firstQuery, uint32_t queryCount, Buffer* dst, uint64_t dstOffset)
{
  CommandList()->ResolveQueryData(ToBackend(querySet)->QueryHeap(), D3D12QueryType(querySet->Type()), firstQuery, queryCount, ToBackend(dst)->Resource(), dstOffset);
}

void CommandEncoderImpl::WriteTimestamp(QuerySet* querySet, uint32_t index)
{
  D3D12WriteTimestamp(CommandList(), querySet, index);
}

CommandBuffer* CommandEncoderImpl::Finish()
{
  ToBackend(m_CommandBuffer)->CommandList()->Close();

  CommandBuffer* ptr = m_CommandBuffer;
  m_CommandBuffer = nullptr;

  return ptr;
}

ComputePassEncoderImpl::ComputePassEncoderImpl(const ComputePassDesc& desc, CommandBuffer* commandBuffer) : ComputePassEncoder(desc, commandBuffer) {}

ComputePassEncoderImpl::~ComputePassEncoderImpl() = default;

void ComputePassEncoderImpl::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
  CommandList()->Dispatch(x, y, z);
}

void ComputePassEncoderImpl::End()
{
  if (m_Desc.timestampWrites.has_value()) {
    D3D12WriteTimestamp(CommandList(), m_Desc.timestampWrites->querySet, m_Desc.timestampWrites->endOfPassWriteIndex);
  }

  m_Ended = true;
}

void ComputePassEncoderImpl::PushConstants(uint32_t offset, uint32_t size, const void* data)
{
  CommandList()->SetComputeRoot32BitConstants(0, size, data, offset);
}

void ComputePassEncoderImpl::SetPipeline(ComputePipeline* pipeline)
{
  CommandList()->SetPipelineState(ToBackend(pipeline)->PipelineState());
}

RenderPassEncoderImpl::RenderPassEncoderImpl(const RenderPassDesc& desc, CommandBuffer* commandBuffer) : RenderPassEncoder(desc, commandBuffer) {}

RenderPassEncoderImpl::~RenderPassEncoderImpl() = default;

void RenderPassEncoderImpl::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
  // TODO: track current pipeline / asssert render pipeline?

  CommandList()->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void RenderPassEncoderImpl::DrawMeshIndirect(Buffer* indirectBuffer, uint64_t indirectOffset, uint32_t maxDrawCount, Buffer* countBuffer, uint64_t countOffset)
{
  ID3D12Resource* pCountBuffer = nullptr;
  if (countBuffer != nullptr) {
    pCountBuffer = ToBackend(countBuffer)->Resource();
  }

  // TODO: track current pipeline / asssert mesh pipeline?

  CommandList()->ExecuteIndirect(ToBackend(m_CommandBuffer->GetDevice())->DispatchMeshCommandSignature(), maxDrawCount, ToBackend(indirectBuffer)->Resource(), indirectOffset, pCountBuffer, countOffset);
}

void RenderPassEncoderImpl::End()
{
  // TODO: mark encoder as open = false
  // TODO: ResolveSubresource if MSAA (resolveTarget)

  if (m_Desc.timestampWrites.has_value()) {
    D3D12WriteTimestamp(CommandList(), m_Desc.timestampWrites->querySet, m_Desc.timestampWrites->endOfPassWriteIndex);
  }

  m_Ended = true;
}

void RenderPassEncoderImpl::PushConstants(uint32_t offset, uint32_t size, const void* data)
{
  CommandList()->SetGraphicsRoot32BitConstants(0, size, data, offset);
}

void RenderPassEncoderImpl::SetPipeline(RenderPipeline* pipeline)
{
  CommandList()->IASetPrimitiveTopology(ToBackend(pipeline)->NativePrimitiveTopology());
  CommandList()->SetPipelineState(ToBackend(pipeline)->PipelineState());
}

RayTracingPassEncoderImpl::RayTracingPassEncoderImpl(const RayTracingPassDesc& desc, CommandBuffer* commandBuffer) : RayTracingPassEncoder(desc, commandBuffer) {}

RayTracingPassEncoderImpl::~RayTracingPassEncoderImpl() = default;

void RayTracingPassEncoderImpl::End()
{
  if (m_Desc.timestampWrites.has_value()) {
    D3D12WriteTimestamp(CommandList(), m_Desc.timestampWrites->querySet, m_Desc.timestampWrites->endOfPassWriteIndex);
  }

  m_Ended = true;
}

void RayTracingPassEncoderImpl::PushConstants(uint32_t offset, uint32_t size, const void* data)
{
  CommandList()->SetComputeRoot32BitConstants(0, size, data, offset);
}

void RayTracingPassEncoderImpl::SetPipeline(RayTracingPipeline* pipeline)
{
  CommandList()->SetPipelineState1(ToBackend(pipeline)->StateObject());
}

void RayTracingPassEncoderImpl::TraceRays(ShaderTable* shaderTable, uint32_t width, uint32_t height, uint32_t depth)
{
  D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

  dispatchDesc.RayGenerationShaderRecord = ToBackend(shaderTable)->RayGenShaderRecord();
  dispatchDesc.MissShaderTable = ToBackend(shaderTable)->MissShaderTable();
  dispatchDesc.HitGroupTable = ToBackend(shaderTable)->HitGroupTable();
  dispatchDesc.CallableShaderTable = ToBackend(shaderTable)->CallableShaderTable();

  dispatchDesc.Width = width;
  dispatchDesc.Height = height;
  dispatchDesc.Depth = depth;

  CommandList()->DispatchRays(&dispatchDesc);
}
}  // namespace D3D12
}  // namespace IssouRHI
