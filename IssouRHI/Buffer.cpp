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

  Unmap();

  if (m_Allocation) {
    m_Allocation->Release();
    m_Allocation = nullptr;
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

BufferRange Buffer::ClampBufferRange(BufferRange range)
{
  range.offset = std::min(range.offset, Size());
  range.size = std::min(range.size, Size() - range.offset);

  return range;
}

void Buffer::Write(BufferRange range, const void* data)
{
  assert(m_Desc.usage & BufferUsage::MapWrite);

  range = ClampBufferRange(range);

  Map();

  memcpy((BYTE*)m_Address + range.offset, data, range.size);
}

void Buffer::Clear(BufferRange range)
{
  assert(m_Desc.usage & BufferUsage::MapWrite);

  range = ClampBufferRange(range);

  Map();

  ZeroMemory((BYTE*)m_Address + range.offset, range.size);
}

void Buffer::Read(BufferRange range, void* outData)
{
  assert(m_Desc.usage & BufferUsage::MapRead);

  range = ClampBufferRange(range);

  Map();

  // TODO: callback version without memcpy?
  memcpy(outData, (BYTE*)m_Address + range.offset, range.size);
}

void Buffer::Map()
{
  if (m_Address != nullptr) return;

  D3D12_RANGE empty = {0, 0};
  D3D12_RANGE full = {0, Size()};
  D3D12_RANGE* range = nullptr;
  if (m_Desc.usage & BufferUsage::MapWrite) {
    range = &empty;
  } else if (m_Desc.usage & BufferUsage::MapRead) {
    range = &full;
  } else {
    std::unreachable();
  }

  CHECK_HR(m_Resource->Map(0, range, &m_Address));
}

void Buffer::Unmap()
{
  if (m_Address == nullptr) return;

  m_Resource->Unmap(0, nullptr);
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

DescriptorAllocation Buffer::SrvDescriptorAlloc(BufferRange range, UINT byteStride)
{
  range = ClampBufferRange(range);

  ViewKey k{.range = range, .byteStride = byteStride};
  auto& alloc = m_Srvs[k];

  if (alloc) {
    return alloc;
  }

  alloc = m_Device->AllocSrvUavDescriptor();

  auto srvDesc = SrvDescriptor(range, byteStride);
  m_Device->GetNativeDevice()->CreateShaderResourceView(Resource(), &srvDesc, alloc.cpuHandle);

  return alloc;
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

DescriptorAllocation Buffer::UavDescriptorAlloc(BufferRange range,
                                                UINT byteStride,
                                                Buffer* counter,
                                                UINT64 counterOffsetInBytes)
{
  range = ClampBufferRange(range);

  ViewKey k{.range = range, .byteStride = byteStride, .counter = counter, .counterOffsetInBytes = counterOffsetInBytes};
  auto& alloc = m_Uavs[k];

  if (alloc) {
    return alloc;
  }

  alloc = m_Device->AllocSrvUavDescriptor();

  auto uavDesc = UavDescriptor(range, byteStride, counterOffsetInBytes);
  m_Device->GetNativeDevice()->CreateUnorderedAccessView(Resource(), counter ? counter->Resource() : nullptr, &uavDesc,
                                                         alloc.cpuHandle);

  return alloc;
}
}  // namespace IssouRHI
