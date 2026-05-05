#include "QuerySetD3D12.h"

#include "DeviceD3D12.h"
#include "UtilsD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
QuerySetImpl::QuerySetImpl(Device* device, const QuerySetDesc& desc) : QuerySet(device, desc) {}

QuerySetImpl::~QuerySetImpl() = default;

static D3D12_QUERY_HEAP_TYPE D3D12QueryHeapType(QueryType type)
{
  switch (type) {
    case QueryType::Timestamp:
      return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    default:
      std::unreachable();
  }
}

// TODO: method that use CHECK_HR should be bool/result?
void QuerySetImpl::Create()
{
  D3D12_QUERY_HEAP_DESC queryHeapDesc{};
  queryHeapDesc.Type = D3D12QueryHeapType(m_Desc.type);
  queryHeapDesc.Count = m_Desc.count;

  CHECK_HR(ToBackend(m_Device)->GetNativeDevice()->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_QueryHeap)));

  m_QueryHeap->SetName(StringToWstring(m_Desc.label).c_str());
}
}
}
