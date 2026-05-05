#include "IssouRHI.h"

namespace IssouRHI
{
QuerySet::QuerySet(Device* device, const QuerySetDesc& desc) : m_Device(device), m_Desc(desc) {}

QuerySet::~QuerySet() = default;


}  // namespace IssouRHI
