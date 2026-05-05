#pragma once

#include "CommonD3D12.h"
#include "DescriptorHeapD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class ShaderTableImpl : public ShaderTable
{
public:
   ShaderTableImpl(Device* device);
   ~ShaderTableImpl() override;

   void Create(const ShaderTableDesc& desc) override;

public:
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE RayGenShaderRecord() const;
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE MissShaderTable() const;
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE HitGroupTable() const;
  D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE CallableShaderTable() const;
};

inline ShaderTableImpl* ToBackend(ShaderTable* st) { return static_cast<ShaderTableImpl*>(st); }
}
}
