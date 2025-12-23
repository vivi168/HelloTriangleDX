#include "IssouRHI.h"
#include "Utils.h"

namespace IssouRHI
{
Buffer::Buffer(Device* device, BufferDesc& desc) : m_Device(device), m_Desc(desc) {}

Buffer::~Buffer()
{
  for (auto& [_, alloc] : m_Srvs) {
    m_Device->FreeSrvUavDescriptor(alloc);
  }

  for (auto& [_, alloc] : m_Uavs) {
    m_Device->FreeSrvUavDescriptor(alloc);
  }
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

void Buffer::Copy(BufferRange& range, const void* data)
{
  if (!m_Mapped) {
    Map();
  }

  memcpy((BYTE*)m_Address + range.offset, data, range.size);
}

void Buffer::Clear(BufferRange& range)
{
  if (!m_Mapped) {
    Map();
  }

  ZeroMemory((BYTE*)m_Address + range.offset, range.size);
}

void Buffer::Map()
{
  assert(!m_Mapped);

  CHECK_HR(m_Resource->Map(0, &EMPTY_RANGE, &m_Address));
  m_Mapped = true;
}

void Buffer::Unmap()
{
  assert(m_Mapped);

  m_Resource->Unmap(0, nullptr);
  m_Mapped = false;
}

D3D12_RESOURCE_BARRIER Buffer::Transition(D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter)
{
  return CD3DX12_RESOURCE_BARRIER::Transition(Resource(), stateBefore, stateAfter);
}

static D3D12_SHADER_RESOURCE_VIEW_DESC SrvDescriptor(BufferRange& range, UINT byteStride)
{
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                          .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                                          .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                                          .Buffer = {
                                              .FirstElement = range.offset / byteStride,
                                              .NumElements = static_cast<UINT>(range.size / byteStride),
                                              .StructureByteStride = byteStride,
                                              .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                                          }};
  return srvDesc;
}

D3D12_CPU_DESCRIPTOR_HANDLE Buffer::SrvDescriptorHandle(BufferRange& range, UINT byteStride)
{
  ViewKey k{.range = range, .byteStride = byteStride};
  auto& alloc = m_Srvs[k];

  if (alloc) {
    return alloc.cpuHandle;
  }

  alloc = m_Device->AllocSrvUavDescriptor();

  auto srvDesc = SrvDescriptor(range, byteStride);
  m_Device->GetNativeDevice()->CreateShaderResourceView(Resource(), &srvDesc, alloc.cpuHandle);

  return alloc.cpuHandle;
}

static D3D12_UNORDERED_ACCESS_VIEW_DESC UavDescriptor(BufferRange& range, UINT byteStride, UINT64 counterOffsetInBytes)
{
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{.Format = DXGI_FORMAT_UNKNOWN,
                                           .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                                           .Buffer = {.FirstElement = range.offset / byteStride,
                                                      .NumElements = static_cast<UINT>(range.size / byteStride),
                                                      .StructureByteStride = byteStride,
                                                      .CounterOffsetInBytes = counterOffsetInBytes,
                                                      .Flags = D3D12_BUFFER_UAV_FLAG_NONE}};

  return uavDesc;
}

D3D12_CPU_DESCRIPTOR_HANDLE Buffer::UavDescriptorHandle(BufferRange& range,
                                                        UINT byteStride,
                                                        Buffer* counter,
                                                        UINT64 counterOffsetInBytes)
{
  ViewKey k{.range = range, .byteStride = byteStride, .counter = counter, .counterOffsetInBytes = counterOffsetInBytes};
  auto& alloc = m_Uavs[k];

  if (alloc) {
    return alloc.cpuHandle;
  }

  alloc = m_Device->AllocSrvUavDescriptor();

  auto uavDesc = UavDescriptor(range, byteStride, counterOffsetInBytes);
  m_Device->GetNativeDevice()->CreateUnorderedAccessView(Resource(), counter ? counter->Resource() : nullptr, &uavDesc,
                                                         alloc.cpuHandle);

  return alloc.cpuHandle;
}
}  // namespace IssouRHI
