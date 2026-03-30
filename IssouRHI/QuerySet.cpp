#include "IssouRHI.h"

namespace IssouRHI
{
QuerySet::QuerySet(Device* device, const QuerySetDesc& desc) : m_Device(device), m_Desc(desc) {}

QuerySet::~QuerySet() = default;

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
void QuerySet::Create()
{
  D3D12_QUERY_HEAP_DESC queryHeapDesc{
      .Type = D3D12QueryHeapType(m_Desc.type),
      .Count = m_Desc.count,
  };
  CHECK_HR(m_Device->GetNativeDevice()->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_QueryHeap)));
}
}
