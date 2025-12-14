#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
Buffer::Buffer(Device* device, BufferDesc& desc) : m_Device(device), m_Desc(desc) {}

Buffer::~Buffer()
{
  // TODO: Free Srv/Uav Descriptors
}

void Buffer::Attach(ID3D12Resource* other, D3D12MA::Allocation* allocation)
{
  m_Resource.Attach(other);
  m_Allocation = allocation;
}

void Buffer::InitState(D3D12_RESOURCE_STATES initialResourceState, bool fixedResourceState)
{
  m_CurrentState = initialResourceState;
  m_FixedResourceState = fixedResourceState;
}

void Buffer::Copy(BufferRange range, const void* data)
{  // TODO
}

D3D12_RESOURCE_BARRIER BufferTransition(D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{  // TODO
}

D3D12_CPU_DESCRIPTOR_HANDLE Buffer::SrvDescriptorHandle(BufferRange range, UINT byteStride)
{
  // TODO
}

D3D12_CPU_DESCRIPTOR_HANDLE Buffer::UavDescriptorHandle(BufferRange range,
                                                        UINT byteStride,
                                                        Buffer* counter,
                                                        UINT64 counterOffsetInBytes)
{
  // TODO
}
}  // namespace IssouRHI
