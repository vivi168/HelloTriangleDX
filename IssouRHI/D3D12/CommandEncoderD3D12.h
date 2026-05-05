#pragma once

#include "CommonD3D12.h"
#include "QueueD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class CommandEncoderImpl : public CommandEncoder
{
public:
  CommandEncoderImpl(std::string label, CommandBuffer* commandBuffer);
  ~CommandEncoderImpl() override;

  // TODO: make it so that we can't begin a pass if another one hasn't yet ended
  std::unique_ptr<ComputePassEncoder> BeginComputePass(const ComputePassDesc& desc) override;
  std::unique_ptr<RenderPassEncoder> BeginRenderPass(const RenderPassDesc& desc) override;
  std::unique_ptr<RayTracingPassEncoder> BeginRayTracingPass(const RayTracingPassDesc& desc) override;

  // TODO: make these uncallable if a pass has begun+not yet ended?
  void Barrier(const BarriersDesc& desc) override;
  void BuildTopLevelAccelerationStructure(AccelerationStructure* dst, BufferWithOffset instances, uint32_t instanceCount, AccelerationStructure* src = nullptr) override;
  void BuildBottomLevelAccelerationStructure(AccelerationStructure* dst, std::span<BottomLevelGeometryDesc> geometries, AccelerationStructure* src = nullptr) override;
  void CopyBufferToBuffer(Buffer* src, uint64_t srcOffset, Buffer* dst, uint64_t dstOffset, uint64_t size) override;
  void ResolveQuerySet(QuerySet* querySet, uint32_t firstQuery, uint32_t queryCount, Buffer* dst, uint64_t dstOffset) override;
  void WriteTimestamp(QuerySet* querySet, uint32_t index) override;

  // TODO: BuildOpacityMicroMaps

  // TODO: assert that every pass has ended
  CommandBuffer* Finish() override;

public:
  ID3D12GraphicsCommandList8* CommandList() const { return ToBackend(m_CommandBuffer)->CommandList(); }
};

class ComputePassEncoderImpl : public ComputePassEncoder
{
public:
  ComputePassEncoderImpl(const ComputePassDesc& desc, CommandBuffer* commandBuffer);
  ~ComputePassEncoderImpl() override;

  void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1) override;
  // TODO: DispatchIndirect(indirectBuffer, indirectOffset) override
  void End() override;
  void PushConstants(uint32_t offset, uint32_t size, const void* data) override;
  void SetPipeline(ComputePipeline* pipeline) override;

public:
  ID3D12GraphicsCommandList8* CommandList() const { return ToBackend(m_CommandBuffer)->CommandList(); }
};

class RenderPassEncoderImpl : public RenderPassEncoder
{
public:
  RenderPassEncoderImpl(const RenderPassDesc& desc, CommandBuffer* commandBuffer);
  ~RenderPassEncoderImpl() override;

  void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
  // TODO: DrawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
  // TODO: DrawIndirect(indirectBuffer, indirectOffset)
  // TODO: DrawIndexedIndirect(indirectBuffer, indirectOffset)
  // TODO: DrawMesh();
  void DrawMeshIndirect(Buffer* indirectBuffer, uint64_t indirectOffset, uint32_t maxDrawCount, Buffer* countBuffer = nullptr, uint64_t countOffset = 0) override;
  void End() override;
  void PushConstants(uint32_t offset, uint32_t size, const void* data) override;
  void SetPipeline(RenderPipeline* pipeline) override;

public:
  ID3D12GraphicsCommandList8* CommandList() const { return ToBackend(m_CommandBuffer)->CommandList(); }
};

class RayTracingPassEncoderImpl : public RayTracingPassEncoder
{
public:
  RayTracingPassEncoderImpl(const RayTracingPassDesc& desc, CommandBuffer* commandBuffer);
  ~RayTracingPassEncoderImpl() override;

  void End() override;
  void PushConstants(uint32_t offset, uint32_t size, const void* data) override;
  void SetPipeline(RayTracingPipeline* pipeline) override;
  void TraceRays(ShaderTable* shaderTable, uint32_t width, uint32_t height, uint32_t depth = 1) override;
  // TODO: TraceRaysIndirect

public:
  ID3D12GraphicsCommandList8* CommandList() const { return ToBackend(m_CommandBuffer)->CommandList(); }
};
}  // namespace D3D12
}  // namespace IssouRHI
