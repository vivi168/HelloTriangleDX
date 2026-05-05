#pragma once

#include "CommonD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class QuerySetImpl : public QuerySet
{
public:
  QuerySetImpl(Device* device, const QuerySetDesc& desc);
  ~QuerySetImpl() override;

  void Create() override;

public:
  ID3D12QueryHeap* QueryHeap() const { return m_QueryHeap.Get(); }

private:
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_QueryHeap;
};

inline QuerySetImpl* ToBackend(QuerySet* qs) { return static_cast<QuerySetImpl*>(qs); }
}
}
