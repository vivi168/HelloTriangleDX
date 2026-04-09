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

void Queue::Create()
{
  D3D12_COMMAND_QUEUE_DESC qDesc{
      .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
      .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
  };

  ID3D12CommandQueue* commandQueue = nullptr;
  CHECK_HR(m_Device->GetNativeDevice()->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&commandQueue)));
  m_CommandQueue.Attach(commandQueue);
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
    cb->UpdateFenceValue(m_NextFenceValue);
    m_CommandBuffersExecuting.push_back(cb);
    cmdLists.push_back(static_cast<ID3D12CommandList*>(cb->CommandList()));
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
  auto cb = std::make_unique<CommandBuffer>(m_Device);

  cb->Create();

  CommandBuffer* ptr = cb.get();
  m_CommandBuffers.push_back(std::move(cb));

  printf("CREATE COMMAND BUFFER\n");

  return ptr;
}

void Queue::RecycleCommandBuffers()
{
  UINT64 completedValue = m_Fence->GetCompletedValue();

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

CommandBuffer::~CommandBuffer()
{
  // TODO
}

void CommandBuffer::Create()
{
  ID3D12CommandAllocator* commandAllocator = nullptr;
  CHECK_HR(m_Device->GetNativeDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
  m_CommandAllocator.Attach(commandAllocator);

  CHECK_HR(m_Device->GetNativeDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator.Get(), NULL, IID_PPV_ARGS(&m_CommandList)));

  Init();
}

void CommandBuffer::Init()
{
  // TODO: also set sampler heap
  std::array descriptorHeaps{m_Device->CbvSrvUavDescriptorHeap()};
  m_CommandList->SetDescriptorHeaps(static_cast<UINT>(descriptorHeaps.size()), descriptorHeaps.data());
}

void CommandBuffer::Reset()
{
  CHECK_HR(m_CommandAllocator->Reset());
  CHECK_HR(m_CommandList->Reset(m_CommandAllocator.Get(), nullptr));

  m_ComputeRootSignatureSet = false;
  m_GraphicRootSignatureSet = false;
}

void CommandBuffer::SetComputeRootSignatureIfNeeded()
{
  if (m_ComputeRootSignatureSet) return;

  m_CommandList->SetComputeRootSignature(m_Device->RootSignature());
  m_ComputeRootSignatureSet = true;
}

void CommandBuffer::SetGraphicsRootSignatureIfNeeded()
{
  if (m_GraphicRootSignatureSet) return;

  m_CommandList->SetGraphicsRootSignature(m_Device->RootSignature());
  m_GraphicRootSignatureSet = true;
}

}  // namespace IssouRHI
