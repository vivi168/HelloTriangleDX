#pragma once

#include "CommonD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
class ComputePipelineImpl : public ComputePipeline
{
public:
  ComputePipelineImpl(Device* device);
  ~ComputePipelineImpl() override;

  void Create(const ComputePipelineDesc& desc) override;

public:
  ID3D12PipelineState* PipelineState() const { return m_Pso.Get(); }

private:
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pso;
};

inline ComputePipelineImpl* ToBackend(ComputePipeline* pipeline) { return static_cast<ComputePipelineImpl*>(pipeline); }
inline const ComputePipelineImpl* ToBackend(const ComputePipeline* pipeline) { return static_cast<const ComputePipelineImpl*>(pipeline); }

class RenderPipelineImpl : public RenderPipeline
{
public:
  RenderPipelineImpl(Device* device, Type type);
  ~RenderPipelineImpl() override;

  void Create(const RenderPipelineDesc& desc) override;

public:
  ID3D12PipelineState* PipelineState() const { return m_Pso.Get(); }

  D3D12_PRIMITIVE_TOPOLOGY NativePrimitiveTopology() const { return m_PrimitiveTopology; }

private:
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_Pso;

  D3D12_PRIMITIVE_TOPOLOGY m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
};

inline RenderPipelineImpl* ToBackend(RenderPipeline* pipeline) { return static_cast<RenderPipelineImpl*>(pipeline); }
inline const RenderPipelineImpl* ToBackend(const RenderPipeline* pipeline) { return static_cast<const RenderPipelineImpl*>(pipeline); }

class RayTracingPipelineImpl : public RayTracingPipeline
{
public:
  RayTracingPipelineImpl(Device* device);
  ~RayTracingPipelineImpl() override;

  void Create(const RayTracingPipelineDesc& desc) override;

public:
  ID3D12StateObject* StateObject() const { return m_StateObject.Get(); }

  void* ShaderIdentifier(std::string entryPoint) const;

private:
  Microsoft::WRL::ComPtr<ID3D12StateObject> m_StateObject;
  Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> m_StateObjectProperties;
};

inline RayTracingPipelineImpl* ToBackend(RayTracingPipeline* pipeline) { return static_cast<RayTracingPipelineImpl*>(pipeline); }
inline const RayTracingPipelineImpl* ToBackend(const RayTracingPipeline* pipeline) { return static_cast<const RayTracingPipelineImpl*>(pipeline); }
}  // namespace D3D12
}  // namespace IssouRHI
