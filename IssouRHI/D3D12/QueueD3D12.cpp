#include "QueueD3D12.h"

#include "CommandEncoderD3D12.h"
#include "DescriptorHeapD3D12.h"
#include "DeviceD3D12.h"
#include "UtilsD3D12.h"

#include <array>

namespace IssouRHI
{
namespace D3D12
{
CommandBufferImpl::CommandBufferImpl(Device* device) : CommandBuffer(device) {}

CommandBufferImpl::~CommandBufferImpl() = default;

void CommandBufferImpl::Create()
{
  ID3D12CommandAllocator* commandAllocator = nullptr;
  CHECK_HR(ToBackend(m_Device)->GetNativeDevice()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
  m_CommandAllocator.Attach(commandAllocator);

  CHECK_HR(ToBackend(m_Device)->GetNativeDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocator.Get(), NULL, IID_PPV_ARGS(&m_CommandList)));

  Init();
}

void CommandBufferImpl::Init()
{
  // TODO: also set sampler heap
  std::array descriptorHeaps{ToBackend(m_Device)->CbvSrvUavDescriptorHeap()};
  m_CommandList->SetDescriptorHeaps(static_cast<UINT>(descriptorHeaps.size()), descriptorHeaps.data());
}

void CommandBufferImpl::Reset()
{
  CHECK_HR(m_CommandAllocator->Reset());
  CHECK_HR(m_CommandList->Reset(m_CommandAllocator.Get(), nullptr));

  m_ComputeRootSignatureSet = false;
  m_GraphicRootSignatureSet = false;
}

void CommandBufferImpl::SetComputeRootSignatureIfNeeded()
{
  if (m_ComputeRootSignatureSet) return;

  m_CommandList->SetComputeRootSignature(ToBackend(m_Device)->RootSignature());
  m_ComputeRootSignatureSet = true;
}

void CommandBufferImpl::SetGraphicsRootSignatureIfNeeded()
{
  if (m_GraphicRootSignatureSet) return;

  m_CommandList->SetGraphicsRootSignature(ToBackend(m_Device)->RootSignature());
  m_GraphicRootSignatureSet = true;
}

QueueImpl::QueueImpl(Device* device) : Queue(device) {}

QueueImpl::~QueueImpl()
{
  WaitForAll();
  CloseHandle(m_FenceEvent);
}

void QueueImpl::Create()
{
  D3D12_COMMAND_QUEUE_DESC qDesc{};
  qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  qDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  ID3D12CommandQueue* commandQueue = nullptr;
  CHECK_HR(ToBackend(m_Device)->GetNativeDevice()->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&commandQueue)));
  m_CommandQueue.Attach(commandQueue);

  {
    ID3D12Fence* fence = nullptr;
    CHECK_HR(ToBackend(m_Device)->GetNativeDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    m_Fence.Attach(fence);

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_FenceEvent);
  }

  // TODO: should we pre-create a few CommandBuffer?
}

void QueueImpl::Submit(std::span<CommandBuffer*> commandBuffers)
{
  m_NextFenceValue++;

  std::vector<ID3D12CommandList*> cmdLists;
  cmdLists.reserve(commandBuffers.size());

  for (auto cb : commandBuffers) {
    cb->UpdateFenceValue(m_NextFenceValue);
    m_CommandBuffersExecuting.push_back(cb);
    cmdLists.push_back(static_cast<ID3D12CommandList*>(ToBackend(cb)->CommandList()));
  }

  if (!cmdLists.empty()) {
    m_CommandQueue->ExecuteCommandLists(static_cast<UINT>(cmdLists.size()), cmdLists.data());
  }

  CHECK_HR(m_CommandQueue->Signal(m_Fence.Get(), m_NextFenceValue));

  RecycleCommandBuffers();
}

void QueueImpl::WaitForAll()
{
  // Wait for all executing command buffer to finish
  const UINT64 fenceValue = m_NextFenceValue++;
  CHECK_HR(m_CommandQueue->Signal(m_Fence.Get(), fenceValue));

  if (m_Fence->GetCompletedValue() < fenceValue) {
    CHECK_HR(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent));
    WaitForSingleObject(m_FenceEvent, INFINITE);
  }
}

std::unique_ptr<CommandEncoder> QueueImpl::CreateCommandEncoderImpl(std::string label, CommandBuffer* commandBuffer)
{
  return std::make_unique<CommandEncoderImpl>(label, commandBuffer);
}

std::unique_ptr<CommandBuffer> QueueImpl::CreateCommandBufferImpl()
{
  return std::make_unique<CommandBufferImpl>(m_Device);
}
}  // namespace D3D12
}  // namespace IssouRHI
