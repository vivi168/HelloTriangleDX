#include "IssouRHI.h"

namespace IssouRHI
{
AccelerationStructure::AccelerationStructure(Device* device) : m_Device(device) {}

AccelerationStructure::~AccelerationStructure() = default;
}  // namespace IssouRHI
