#include "IssouRHI.h"

namespace IssouRHI
{
CommandEncoder::CommandEncoder(std::string label, CommandBuffer* commandBuffer) : m_CommandBuffer(commandBuffer), m_Label(label) {}

CommandEncoder::~CommandEncoder()
{
  assert(m_CommandBuffer == nullptr);  // Forgot to call Finish()
  // or instead of assert'ing simply close cmd list and recycle cmd buffer since it was not used?
}

ComputePassEncoder::ComputePassEncoder(const ComputePassDesc& desc, CommandBuffer* commandBuffer) : m_CommandBuffer(commandBuffer), m_Desc(desc) {}

ComputePassEncoder::~ComputePassEncoder()
{
  assert(m_Ended);
}

RenderPassEncoder::RenderPassEncoder(const RenderPassDesc& desc, CommandBuffer* commandBuffer) : m_CommandBuffer(commandBuffer), m_Desc(desc) {}

RenderPassEncoder::~RenderPassEncoder()
{
  assert(m_Ended);
}

RayTracingPassEncoder::RayTracingPassEncoder(const RayTracingPassDesc& desc, CommandBuffer* commandBuffer) : m_CommandBuffer(commandBuffer), m_Desc(desc) {}

RayTracingPassEncoder::~RayTracingPassEncoder()
{
  assert(m_Ended);
}

}  // namespace IssouRHI
