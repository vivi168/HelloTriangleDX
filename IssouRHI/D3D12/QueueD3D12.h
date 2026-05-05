#pragma once

#include "CommonD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class CommandBufferImpl : public CommandBuffer
{
public:
   CommandBufferImpl(Device* device);
   ~CommandBufferImpl() override;

   void Create() override;
   void Init() override;
   void Reset() override;

public:
  void SetComputeRootSignatureIfNeeded();
  void SetGraphicsRootSignatureIfNeeded();

  ID3D12GraphicsCommandList8* CommandList() const { return m_CommandList.Get(); }

private:
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandAllocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList8> m_CommandList;

  bool m_ComputeRootSignatureSet = false;
  bool m_GraphicRootSignatureSet = false;

  // TODO: keep track of pso change
  // TODO: basic resource state tracking?
};

inline CommandBufferImpl* ToBackend(CommandBuffer* cb) { return static_cast<CommandBufferImpl*>(cb); }

class QueueImpl : public Queue
{
public:
  QueueImpl(Device* device);
  ~QueueImpl() override;

  void Create() override;

  void Submit(std::span<CommandBuffer*> commandBuffers) override;
  void WaitForAll() override;

public:
  ID3D12CommandQueue* GetNativeQueue() const { return m_CommandQueue.Get(); }

protected:
  std::unique_ptr<CommandEncoder> CreateCommandEncoderImpl(std::string label, CommandBuffer* commandBuffer) override;
  std::unique_ptr<CommandBuffer> CreateCommandBufferImpl() override;

  uint64_t FenceCompletedValue() const override { return m_Fence->GetCompletedValue(); }

private:
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;

  Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
  UINT64 m_NextFenceValue = 0;
  HANDLE m_FenceEvent = nullptr;
};

inline QueueImpl* ToBackend(Queue* q) { return static_cast<QueueImpl*>(q); }
}
}
