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

static std::optional<D3D12_BUFFER_BARRIER> Transition(Buffer* buf, StageAccess from, StageAccess to)
{
  bool accessChanged = from.access != to.access;
  bool storageBarrier = from.access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS && to.access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

  if (!accessChanged && !storageBarrier)
    return std::nullopt;

  auto barrier = CD3DX12_BUFFER_BARRIER(from.stage,
                                        to.stage,
                                        from.access,
                                        to.access,
                                        buf->Resource());

  return barrier;
}

static std::optional<D3D12_TEXTURE_BARRIER> Transition(Texture* tex, StageAccessLayout from, StageAccessLayout to)
{
  bool accessLayoutChanged = from.access != to.access || from.layout != to.layout;
  bool storageBarrier = from.access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS && to.access == D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;

  if (!accessLayoutChanged && !storageBarrier)
    return std::nullopt;

  auto barrier = CD3DX12_TEXTURE_BARRIER(from.stage,
                                         to.stage,
                                         from.access,
                                         to.access,
                                         from.layout,
                                         to.layout,
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
