#include "IssouRHI.h"

#include <array>

// I need to sleep
namespace IssouRHI
{

Queue::Queue(Device* device) : m_Device(device)
{
  ID3D12Fence* fence = nullptr;
  CHECK_HR(device->GetNativeDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  m_Fence.Attach(fence);

  m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  assert(m_FenceEvent);

  // TODO: should we pre-create a few CommandBuffer?
}

Queue::~Queue()
{
  WaitForAll();
  CloseHandle(m_FenceEvent);
}

CommandEncoder Queue::CreateCommandEncoder(std::optional<std::string> label)
{
  auto cb = FindOrCreateCommandBuffer();

  return CommandEncoder(label.value_or(""), cb);
}

void Queue::Submit(std::span<CommandBuffer*> commandBuffers)
{
  m_NextFenceValue++;

  std::vector<ID3D12CommandList*> cmdLists;
  cmdLists.reserve(commandBuffers.size());

  for (auto cb : commandBuffers) {
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
  // Wait for all executing command buffer to finish
  const UINT64 fenceValue = m_NextFenceValue++;
  CHECK_HR(m_CommandQueue->Signal(m_Fence.Get(), fenceValue));

  if (m_Fence->GetCompletedValue() < fenceValue) {
    CHECK_HR(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent));
    WaitForSingleObject(m_FenceEvent, INFINITE);
  }
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
  auto cb = std::make_unique<CommandBuffer>();

  ID3D12CommandAllocator* commandAllocator = nullptr;
  CHECK_HR(m_Device->GetNativeDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
  cb->commandAllocator.Attach(commandAllocator);

  CHECK_HR(m_Device->GetNativeDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cb->commandAllocator.Get(), NULL, IID_PPV_ARGS(&cb->commandList)));

  std::array descriptorHeaps{m_Device->SrvUavDescriptorHeap()};
  cb->commandList->SetDescriptorHeaps(static_cast<UINT>(descriptorHeaps.size()), descriptorHeaps.data());

  CommandBuffer* ptr = cb.get();
  m_CommandBuffers.push_back(std::move(cb));

  return ptr;
}

void Queue::RecycleCommandBuffers()
{
  UINT64 completedValue = m_Fence->GetCompletedValue();

  for (auto it = m_CommandBuffersExecuting.begin(); it != m_CommandBuffersExecuting.end();) {
    auto cb = *it;
    if (cb->fenceValue <= completedValue) {
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

  std::array descriptorHeaps{m_Device->SrvUavDescriptorHeap()};
  commandBuffer->commandList->SetDescriptorHeaps(static_cast<UINT>(descriptorHeaps.size()), descriptorHeaps.data());
}

void CommandBuffer::Reset()
{
  CHECK_HR(commandAllocator->Reset());
  CHECK_HR(commandList->Reset(commandAllocator.Get(), nullptr));
}

CommandEncoder::CommandEncoder(std::string label, CommandBuffer* commandBuffer) : m_Label(label), m_CommandBuffer(commandBuffer) {}

CommandEncoder::~CommandEncoder()
{
  assert(m_CommandBuffer == nullptr); // Forgot to call Finish() ?
  // or simply close cmd list and recycle cmd buffer since it was not used?
}

CommandBuffer* CommandEncoder::Finish()
{
  m_CommandBuffer->commandList->Close();

  CommandBuffer* ptr = m_CommandBuffer;
  m_CommandBuffer = nullptr;

  return ptr;
}

}  // namespace IssouRHI
