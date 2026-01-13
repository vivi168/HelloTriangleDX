#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
void PipelineBase::Attach(ID3D12PipelineState* pso)
{
  m_Pso.Attach(pso);
}

ComputePipeline::ComputePipeline(Device* device, ComputePipelineDesc& desc) : m_Device(device), m_Desc(desc) {}

ComputePipeline::~ComputePipeline()
{
  // TODO
}
}  // namespace IssouRHI
