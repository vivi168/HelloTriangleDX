#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
Buffer::Buffer(Device* device, BufferDesc& desc) : m_Device(device), m_Desc(desc) {}

Buffer::~Buffer() {}

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
}
