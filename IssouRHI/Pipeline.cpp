#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
ShaderModule::ShaderModule(std::filesystem::path file)
{
  std::ifstream inFile(file, std::ios::in | std::ios::binary | std::ios::ate);

  if (!inFile && file.is_relative()) {
    inFile.open(GetExecutableDirectory() / file, std::ios::in | std::ios::binary | std::ios::ate);
  }

  if (!inFile) throw std::runtime_error("Read ShaderModule");

  const std::streampos len = inFile.tellg();
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  m_Code.resize(size_t(len));

  inFile.seekg(0, std::ios::beg);
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  inFile.read(reinterpret_cast<char*>(m_Code.data()), len);
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  inFile.close();
}

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
  D3D12_SHADER_BYTECODE computeShader = {m_Desc.module->Data(), m_Desc.module->Size()};

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{
    .pRootSignature = m_Device->RootSignature(),
    .CS = computeShader,
  };

  ID3D12PipelineState* pipelineStateObject;
  CHECK_HR(m_Device->GetNativeDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));

  m_Pso.Attach(pipelineStateObject);
}

}  // namespace IssouRHI
