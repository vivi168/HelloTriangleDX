#include "IssouRHI.h"

namespace IssouRHI
{
Queue::Queue(Device* device) : m_Device(device) {}

Queue::~Queue() = default;

std::unique_ptr<CommandEncoder> Queue::CreateCommandEncoder(std::optional<std::string> label)
{
  auto cb = FindOrCreateCommandBuffer();

  return CreateCommandEncoderImpl(label.value_or(""), cb);
}

CommandBuffer* Queue::FindOrCreateCommandBuffer()
{
  if (m_CommandBuffersAvailable.empty()) {
    return CreateCommandBuffer();
  }

  CommandBuffer* cb = m_CommandBuffersAvailable.front();
  m_CommandBuffersAvailable.pop_front();

  return cb;
}

CommandBuffer* Queue::CreateCommandBuffer()
{
  auto cb = CreateCommandBufferImpl();

  cb->Create();

  CommandBuffer* ptr = cb.get();
  m_CommandBuffers.push_back(std::move(cb));

  return ptr;
}

void Queue::RecycleCommandBuffers()
{
  uint64_t completedValue = FenceCompletedValue();

  for (auto it = m_CommandBuffersExecuting.begin(); it != m_CommandBuffersExecuting.end();) {
    auto cb = *it;
    if (cb->FenceValue() <= completedValue) {
      ResetCommandBuffer(cb);
      m_CommandBuffersAvailable.splice(m_CommandBuffersAvailable.end(), m_CommandBuffersExecuting, it++);
    } else {
      it++;
    }
  }
}

void Queue::ResetCommandBuffer(CommandBuffer* commandBuffer)
{
  commandBuffer->Reset();
  commandBuffer->Init();
}

CommandBuffer::CommandBuffer(Device* device) : m_Device(device) {}

CommandBuffer::~CommandBuffer() = default;
}  // namespace IssouRHI
