#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
ID3D12RootSignature* GetRootSignature(ID3D12Device* device)
{
  // TODO: move this to device instead?
  static Microsoft::WRL::ComPtr<ID3D12RootSignature> s_RootSignature = [&]() {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;

    // Root parameters
    constexpr UINT RootParameterCount = 4;
    constexpr UINT ConstantCount = 16;

    CD3DX12_ROOT_PARAMETER rootParameters[RootParameterCount] = {};
    for (UINT i = 0; i < RootParameterCount; i++) {
      rootParameters[i].InitAsConstants(ConstantCount, i);
    }

    // Static sampler
    constexpr UINT StaticSamplerCount = 2;
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[StaticSamplerCount];
    staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);
    staticSamplers[1].Init(1, D3D12_FILTER_ANISOTROPIC);

    // Root Signature
    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(RootParameterCount, rootParameters, StaticSamplerCount,
                                                            staticSamplers, flags);

    ID3DBlob* signatureBlobPtr;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signatureBlobPtr, nullptr));

    ID3D12RootSignature* rs = nullptr;
    CHECK_HR(device->CreateRootSignature(0, signatureBlobPtr->GetBufferPointer(), signatureBlobPtr->GetBufferSize(),
                                         IID_PPV_ARGS(&rs)));
    rootSignature.Attach(rs);

    return rootSignature;
  }();

  return s_RootSignature.Get();
}

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
