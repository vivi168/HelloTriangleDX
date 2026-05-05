#pragma once

#include "CommonD3D12.h"
#include "DescriptorHeapD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class DeviceImpl : public Device
{
public:
  DeviceImpl(const GPUSelection& gpuSelection);
  ~DeviceImpl() override;

  void Create(const GPUSelection& gpuSelection) override;

  void PrintAdapterInformation() override;

  std::shared_ptr<Surface> CreateSurface(void* handle) override;
  std::shared_ptr<QuerySet> CreateQuerySet(const QuerySetDesc& desc) override;
  std::shared_ptr<Texture> CreateTexture(const TextureDesc& desc) override;
  std::shared_ptr<Buffer> CreateBuffer(const BufferDesc& desc) override;
  std::shared_ptr<AccelerationStructure> CreateAccelerationStructure(const AccelerationStructureDesc& desc) override;

  std::shared_ptr<ComputePipeline> CreateComputePipeline(const ComputePipelineDesc& desc) override;
  std::shared_ptr<RenderPipeline> CreateRenderPipeline(const RenderPipelineDesc& desc) override;
  std::shared_ptr<RenderPipeline> CreateMeshPipeline(const RenderPipelineDesc& desc) override;
  std::shared_ptr<RayTracingPipeline> CreateRayTracingPipelinePipeline(const RayTracingPipelineDesc& desc) override;
  std::shared_ptr<ShaderTable> CreateShaderTable(const ShaderTableDesc& desc) override;

public:
  ID3D12Device5* GetNativeDevice() const { return m_Device.Get(); }

  IDXGIAdapter1* GetAdapter() const { return m_Adapter.Get(); }

  D3D12MA::Allocator* GetAllocator() const { return m_Allocator.Get(); }

  ID3D12DescriptorHeap* CbvSrvUavDescriptorHeap() const { return m_CbvSrvUavDescriptorHeap.Get(); }

  ID3D12DescriptorHeap* RtvDescriptorHeap() const { return m_RtvDescriptorHeap.Get(); }

  ID3D12DescriptorHeap* DsvDescriptorHeap() const { return m_DsvDescriptorHeap.Get(); }

  ID3D12RootSignature* RootSignature() const { return m_RootSignature.Get(); }

  ID3D12CommandSignature* DispatchSignature() const { return m_DispatchSignature.Get(); }

  ID3D12CommandSignature* DrawCommandSignature() const { return m_DrawCommandSignature.Get(); }

  ID3D12CommandSignature* DrawIndirectCommandSignature() const { return m_DrawIndirectCommandSignature.Get(); }

  ID3D12CommandSignature* DispatchMeshCommandSignature() const { return m_DispatchMeshCommandSignature.Get(); }

  DescriptorAllocation AllocCbvSrvUavDescriptor();
  DescriptorAllocation AllocRtvDescriptor();
  DescriptorAllocation AllocDsvDescriptor();

  void FreeSrvUavDescriptor(DescriptorAllocation alloc);
  void FreeRtvDescriptor(DescriptorAllocation alloc);
  void FreeDsvDescriptor(DescriptorAllocation alloc);

private:
  Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
  Microsoft::WRL::ComPtr<ID3D12Device5> m_Device;
  Microsoft::WRL::ComPtr<D3D12MA::Allocator> m_Allocator;
  // Used only when ENABLE_CPU_ALLOCATION_CALLBACKS
  D3D12MA::ALLOCATION_CALLBACKS m_AllocationCallbacks;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;
  // For ExecuteIndirect
  Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_DispatchSignature;
  Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_DrawCommandSignature;
  Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_DrawIndirectCommandSignature;
  Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_DispatchMeshCommandSignature;

  DescriptorHeap m_CbvSrvUavDescriptorHeap;
  DescriptorHeap m_RtvDescriptorHeap;
  DescriptorHeap m_DsvDescriptorHeap;
};

inline DeviceImpl* ToBackend(Device* device) { return static_cast<DeviceImpl*>(device); }
}  // namespace D3D12
}  // namespace IssouRHI
