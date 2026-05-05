#include "IssouRHI.h"

namespace IssouRHI
{
Buffer::Buffer(Device* device, const BufferDesc& desc) : m_Device(device), m_Desc(desc) {}

Buffer::~Buffer() = default;

BufferRange Buffer::ClampBufferRange(BufferRange range)
{
  range.offset = std::min(range.offset, Size());
  range.size = std::min(range.size, Size() - range.offset);

  return range;
}

}  // namespace IssouRHI
