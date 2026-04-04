#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
PipelineBase::PipelineBase(Device* device) : m_Device(device) {}

PipelineBase::~PipelineBase()
{
  // TODO
}

ComputePipeline::ComputePipeline(Device* device, const ComputePipelineDesc& desc) : PipelineBase(device), m_Desc(desc) {}

ComputePipeline::~ComputePipeline()
{
  // TODO
}

void ComputePipeline::Create()
{
  D3D12_SHADER_BYTECODE computeShader = {m_Desc.shaderModule->code, m_Desc.shaderModule->size};

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
    .pRootSignature = m_Device->RootSignature(),
    .CS = computeShader,
  };

  ID3D12PipelineState* pipelineStateObject;
  CHECK_HR(m_Device->GetNativeDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));

  m_Pso.Attach(pipelineStateObject);
  m_Desc.shaderModule = nullptr;
}

}  // namespace IssouRHI
