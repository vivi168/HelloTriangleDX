#include "IssouRHI.h"

// I need to sleep
namespace IssouRHI
{

Queue::Queue(Device* device) : m_Device(device) {}

std::shared_ptr<CommandEncoder> Queue::CreateCommandEncoder(std::optional<std::string> label)
{
  auto cb = FindOrCreateCommandBuffer();

  return std::make_shared<CommandEncoder>(label.value_or(""), cb);
}

void Queue::Submit(std::span<std::shared_ptr<CommandBuffer>> commandBuffers)
{
  m_NextFenceValue++;

  std::vector<ID3D12CommandList*> cmdLists;
  cmdLists.reserve(commandBuffers.size());

  for (auto& cb : commandBuffers) {
    cb->fenceValue = m_NextFenceValue;
    m_CommandBuffersExecuting.push_back(cb);
    cmdLists.push_back(static_cast<ID3D12CommandList*>(cb->commandList.Get()));
  }

  if (!cmdLists.empty()) {
    m_CommandQueue->ExecuteCommandLists(static_cast<UINT>(cmdLists.size()), cmdLists.data());
  }

  CHECK_HR(m_CommandQueue->Signal(m_Fence.Get(), m_NextFenceValue));

  RecycleCommandBuffers();
}

void Queue::WaitForAll()
{
  // Wait for fence
}

std::shared_ptr<CommandBuffer> Queue::FindOrCreateCommandBuffer()
{
  std::shared_ptr<CommandBuffer> cb;

  if (m_CommandBuffersAvailable.empty()) {
    cb = CreateCommandBuffer();
  } else {
    cb = m_CommandBuffersAvailable.front();
    m_CommandBuffersAvailable.pop_front();
  }

  return cb;
}

std::shared_ptr<CommandBuffer> Queue::CreateCommandBuffer()
{
  auto cb = std::make_shared<CommandBuffer>();

  ID3D12CommandAllocator* commandAllocator = nullptr;
  CHECK_HR(m_Device->GetNativeDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
  cb->commandAllocator.Attach(commandAllocator);

  CHECK_HR(m_Device->GetNativeDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cb->commandAllocator.Get(), NULL, IID_PPV_ARGS(&cb->commandList)));

  // command lists are created in the recording state so close it now.
  cb->commandList->Close();

  return cb;
}

void Queue::RecycleCommandBuffers()
{
  UINT64 completedValue = m_Fence->GetCompletedValue();

  for (auto it = m_CommandBuffersExecuting.begin(); it != m_CommandBuffersExecuting.end();) {
    auto& cb = *it;
    if (completedValue < cb->fenceValue) {
      it++;
    } else {
      m_CommandBuffersAvailable.splice(m_CommandBuffersAvailable.end(), m_CommandBuffersExecuting, it++);
      cb->Reset();
    }
  }
}

void CommandBuffer::Reset()
{
  CHECK_HR(commandAllocator->Reset());
  CHECK_HR(commandList->Reset(commandAllocator.Get(), nullptr));

  // SetDescriptorHeaps ? we can easily, because we get it from device->SrvUavDescriptorHeap()
}

CommandEncoder::CommandEncoder(std::string label, std::shared_ptr<CommandBuffer> commandBuffer) : m_Label(label), m_CommandBuffer(commandBuffer) {}

CommandEncoder::~CommandEncoder() {}

std::shared_ptr<CommandBuffer> CommandEncoder::Finish()
{
  m_CommandBuffer->commandList->Close();

  return std::move(m_CommandBuffer);
}

}  // namespace IssouRHI
