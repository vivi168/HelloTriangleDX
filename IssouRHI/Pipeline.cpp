#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
void PipelineBase::Attach(ID3D12PipelineState* pso)
{
  m_Pso.Attach(pso);
}

ComputePipeline::ComputePipeline(Device* device) : m_Device(device) {}

ComputePipeline::~ComputePipeline()
{
  // TODO
}
}  // namespace IssouRHI
