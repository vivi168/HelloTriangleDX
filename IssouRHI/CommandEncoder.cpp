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
