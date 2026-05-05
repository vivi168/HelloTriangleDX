#pragma once

#include "CommonD3D12.h"
#include "DescriptorHeapD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class AccelerationStructureImpl : public AccelerationStructure
{
public:
  AccelerationStructureImpl(Device* device);
  ~AccelerationStructureImpl() override;

  void Create(const AccelerationStructureDesc& desc) override;

  uint32_t DescriptorIndex() const override { return m_Srv.index; }

public:
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS Flags() const { return m_Flags; }

private:
  DescriptorAllocation m_Srv;

private:
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS m_Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO m_PrebuildInfo{};
};

inline AccelerationStructureImpl* ToBackend(AccelerationStructure* as) { return static_cast<AccelerationStructureImpl*>(as); }
}  // namespace D3D12
}  // namespace IssouRHI
