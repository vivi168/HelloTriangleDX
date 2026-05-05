#include "BufferD3D12.h"

#include "DeviceD3D12.h"
#include "UtilsD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
BufferImpl::BufferImpl(Device* device, const BufferDesc& desc) : Buffer(device, desc) {}

BufferImpl::~BufferImpl()
{
  for (const auto& [_, alloc] : m_Srvs) {
    ToBackend(m_Device)->FreeSrvUavDescriptor(alloc);
  }

  for (const auto& [_, alloc] : m_Uavs) {
    ToBackend(m_Device)->FreeSrvUavDescriptor(alloc);
  }

  Unmap();

  if (m_Allocation) {
    m_Allocation->Release();
    m_Allocation = nullptr;
  }
}

void BufferImpl::Create()
{
  assert(!((m_Desc.usage & BufferUsage::MapRead) && (m_Desc.usage & BufferUsage::MapWrite)));

  D3D12MA::CALLOCATION_DESC allocDesc = D3D12MA::CALLOCATION_DESC{};
  if (m_Desc.usage & BufferUsage::MapRead) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
  } else if (m_Desc.usage & BufferUsage::MapWrite) {
    allocDesc.HeapType = D3D12_HEAP_TYPE_GPU_UPLOAD;
  } else {
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  }

  auto bufferDesc = CD3DX12_RESOURCE_DESC1::Buffer(m_Desc.size);
  if (m_Desc.usage & BufferUsage::Storage) {
    bufferDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }
  if (m_Desc.usage & BufferUsage::RayTracingAccelerationStructure) {
    bufferDesc.Flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
  }

  ID3D12Resource* resource;
  D3D12MA::Allocation* allocation;
  CHECK_HR(ToBackend(m_Device)->GetAllocator()->CreateResource3(&allocDesc, &bufferDesc, D3D12_BARRIER_LAYOUT_UNDEFINED, nullptr, 0, nullptr, &allocation, IID_PPV_ARGS(&resource)));
  m_Resource.Attach(resource);
  m_Allocation = allocation;

  m_Resource->SetName(StringToWstring(m_Desc.label).c_str());
}

uint32_t BufferImpl::DescriptorIndex(const BufferViewDesc& desc)
{
  switch (desc.access) {
    case BufferAccess::Constant:
      return CbvDescriptorAlloc(desc.range).index;
    case BufferAccess::Read:
      return SrvDescriptorAlloc(desc.range, desc.elementStride).index;
    case BufferAccess::ReadWrite:
      return UavDescriptorAlloc(desc.range, desc.elementStride, desc.counter, desc.counterOffset).index;
    default:
      std::unreachable();
  }
}

void BufferImpl::Write(BufferRange range, const void* data)
{
  assert(m_Desc.usage & BufferUsage::MapWrite);

  range = ClampBufferRange(range);

  Map();

  memcpy((BYTE*)m_Address + range.offset, data, range.size);
}

void BufferImpl::Clear(BufferRange range)
{
  assert(m_Desc.usage & BufferUsage::MapWrite);

  range = ClampBufferRange(range);

  Map();

  ZeroMemory((BYTE*)m_Address + range.offset, range.size);
}

void BufferImpl::Read(BufferRange range, void* outData)
{
  assert(m_Desc.usage & BufferUsage::MapRead);

  range = ClampBufferRange(range);

  Map();

  // TODO: callback version without memcpy?
  memcpy(outData, (BYTE*)m_Address + range.offset, range.size);
}

void BufferImpl::Map()
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

void BufferImpl::Unmap()
{
  if (m_Address == nullptr) return;

  m_Resource->Unmap(0, nullptr);
}

DescriptorAllocation BufferImpl::CbvDescriptorAlloc(BufferRange range)
{
  range = ClampBufferRange(range);

  ViewKey k{.range = range};
  auto& alloc = m_Cbvs[k];

  if (alloc) {
    return alloc;
  }

  alloc = ToBackend(m_Device)->AllocCbvSrvUavDescriptor();

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{
      .BufferLocation = GpuAddress() + range.offset,
      .SizeInBytes = static_cast<UINT>(range.size),
  };
  ToBackend(m_Device)->GetNativeDevice()->CreateConstantBufferView(&cbvDesc, alloc.cpuHandle);

  return alloc;
}

static D3D12_SHADER_RESOURCE_VIEW_DESC SrvDescriptor(const BufferRange& range, UINT byteStride)
{
  assert(byteStride != 0);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
      .Format = DXGI_FORMAT_UNKNOWN,
      .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
      .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
      .Buffer = {
          .FirstElement = range.offset / byteStride,
          .NumElements = static_cast<UINT>(range.size / byteStride),
          .StructureByteStride = byteStride,
          .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
      },
  };
  return srvDesc;
}

DescriptorAllocation BufferImpl::SrvDescriptorAlloc(BufferRange range, UINT byteStride)
{
  range = ClampBufferRange(range);

  if (byteStride == 0) byteStride = 4;

  ViewKey k{.range = range, .byteStride = byteStride};
  auto& alloc = m_Srvs[k];

  if (alloc) {
    return alloc;
  }

  alloc = ToBackend(m_Device)->AllocCbvSrvUavDescriptor();

  auto srvDesc = SrvDescriptor(range, byteStride);
  ToBackend(m_Device)->GetNativeDevice()->CreateShaderResourceView(Resource(), &srvDesc, alloc.cpuHandle);

  return alloc;
}

static D3D12_UNORDERED_ACCESS_VIEW_DESC UavDescriptor(const BufferRange& range, UINT byteStride, UINT64 counterOffsetInBytes)
{
  assert(byteStride != 0);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{
      .Format = DXGI_FORMAT_UNKNOWN,
      .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
      .Buffer = {
          .FirstElement = range.offset / byteStride,
          .NumElements = static_cast<UINT>(range.size / byteStride),
          .StructureByteStride = byteStride,
          .CounterOffsetInBytes = counterOffsetInBytes,
          .Flags = D3D12_BUFFER_UAV_FLAG_NONE,
      },
  };

  return uavDesc;
}

DescriptorAllocation BufferImpl::UavDescriptorAlloc(BufferRange range, UINT byteStride, Buffer* counter, UINT64 counterOffsetInBytes)
{
  range = ClampBufferRange(range);

  if (byteStride == 0) byteStride = 4;

  ViewKey k{.range = range, .byteStride = byteStride, .counter = counter, .counterOffsetInBytes = counterOffsetInBytes};
  auto& alloc = m_Uavs[k];

  if (alloc) {
    return alloc;
  }

  alloc = ToBackend(m_Device)->AllocCbvSrvUavDescriptor();

  auto uavDesc = UavDescriptor(range, byteStride, counterOffsetInBytes);

  ToBackend(m_Device)->GetNativeDevice()->CreateUnorderedAccessView(Resource(), counter ? ToBackend(counter)->Resource() : nullptr, &uavDesc, alloc.cpuHandle);

  return alloc;
}
}  // namespace D3D12
}  // namespace IssouRHI
