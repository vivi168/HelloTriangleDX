#include "IssouRHI.h"

namespace IssouRHI
{
ComputePipeline::ComputePipeline(Device* device) : m_Device(device) {}

ComputePipeline::~ComputePipeline() = default;

RenderPipeline::RenderPipeline(Device* device, Type type) : m_Device(device), m_Type(type) {}

RenderPipeline::~RenderPipeline() = default;

RayTracingPipeline::RayTracingPipeline(Device* device) : m_Device(device) {}

RayTracingPipeline::~RayTracingPipeline() = default;
}  // namespace IssouRHI
