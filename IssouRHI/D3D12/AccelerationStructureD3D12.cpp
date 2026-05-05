#include "AccelerationStructureD3D12.h"

#include "DeviceD3D12.h"
#include "UtilsD3D12.h"

namespace IssouRHI
{
namespace D3D12
{
AccelerationStructureImpl::AccelerationStructureImpl(Device* device) : AccelerationStructure(device) {}

AccelerationStructureImpl::~AccelerationStructureImpl()
{
  ToBackend(m_Device)->FreeSrvUavDescriptor(m_Srv);
}

static D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS D3D12RayTracingAccelerationStructureBuildFlags(AccelerationStructureFlags flags)
{
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

  if (flags & AccelerationStructureFlags::AllowUpdate) {
    buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
  }
  if (flags & AccelerationStructureFlags::AllowCompaction) {
    buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
  }
  if (flags & AccelerationStructureFlags::PreferFastTrace) {
    buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  }
  if (flags & AccelerationStructureFlags::PreferFastBuild) {
    buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
  }
  if (flags & AccelerationStructureFlags::MinimizeMemory) {
    buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;
  }
  if (flags & AccelerationStructureFlags::AllowMicromapUpdate) {
    buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_OMM_UPDATE;
  }
  if (flags & AccelerationStructureFlags::AllowDisableMicromaps) {
    buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_DISABLE_OMMS;
  }

  return buildFlags;
}

static D3D12_RAYTRACING_GEOMETRY_FLAGS D3D12RaytracingGeometryFlags(BottomLevelGeometryFlags flags)
{
  D3D12_RAYTRACING_GEOMETRY_FLAGS geometryFlags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

  if (flags & BottomLevelGeometryFlags::Opaque) {
    geometryFlags |= D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  }
  if (flags & BottomLevelGeometryFlags::NoDuplicateAnyHitInvocation) {
    geometryFlags |= D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
  }

  return geometryFlags;
}

std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> D3D12RaytracingGeometryDescs(std::span<BottomLevelGeometryDesc> geometries)
{
  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(geometries.size());

  for (size_t i = 0; i < geometries.size(); i++) {
    auto& in = geometries[i];
    auto& out = geometryDescs[i];

    out.Flags = D3D12RaytracingGeometryFlags(in.flags);

    if (std::holds_alternative<BottomLevelTrianglesDesc>(in.geometry)) {
      auto& triangles = std::get<BottomLevelTrianglesDesc>(in.geometry);

      out.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

      out.Triangles.Transform3x4 = triangles.transformMatrices.GpuAddress();

      out.Triangles.IndexFormat = DXGIFormat(triangles.indexFormat);
      out.Triangles.VertexFormat = DXGIFormat(triangles.vertexFormat);

      out.Triangles.IndexCount = triangles.indexCount;
      out.Triangles.VertexCount = triangles.vertexCount;

      out.Triangles.IndexBuffer = triangles.indices.GpuAddress();
      out.Triangles.VertexBuffer.StartAddress = triangles.vertices.GpuAddress();
      out.Triangles.VertexBuffer.StrideInBytes = triangles.vertexStride;
    } else if (std::holds_alternative<BottomLevelAABBsDesc>(in.geometry)) {
      auto& aabbs = std::get<BottomLevelAABBsDesc>(in.geometry);

      out.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;

      out.AABBs.AABBCount = aabbs.count;
      out.AABBs.AABBs.StartAddress = aabbs.aabbs.GpuAddress();
      out.AABBs.AABBs.StrideInBytes = aabbs.stride;
    }
  }

  return geometryDescs;
}

void AccelerationStructureImpl::Create(const AccelerationStructureDesc& desc)
{
  m_Flags = D3D12RayTracingAccelerationStructureBuildFlags(desc.flags);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
  inputs.Flags = m_Flags;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

  std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
  if (std::holds_alternative<TopLevelDesc>(desc.geometryOrInstanceDesc)) {
    auto& geometryOrInstanceDesc = std::get<TopLevelDesc>(desc.geometryOrInstanceDesc);

    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.NumDescs = static_cast<UINT>(geometryOrInstanceDesc.instances.size());
  } else if (std::holds_alternative<BottomLevelDesc>(desc.geometryOrInstanceDesc)) {
    auto& geometryOrInstanceDesc = std::get<BottomLevelDesc>(desc.geometryOrInstanceDesc);
    geometryDescs = D3D12RaytracingGeometryDescs(geometryOrInstanceDesc.geometries);

    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.NumDescs = static_cast<UINT>(geometryDescs.size());
    inputs.pGeometryDescs = geometryDescs.data();
  }

  ToBackend(m_Device)->GetNativeDevice()->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &m_PrebuildInfo);
  assert(m_PrebuildInfo.ResultDataMaxSizeInBytes > 0);

  {
    auto scratchSize = std::max(m_PrebuildInfo.ScratchDataSizeInBytes, m_PrebuildInfo.UpdateScratchDataSizeInBytes);
    assert(scratchSize > 0);

    IssouRHI::BufferDesc scratchDesc{
        .label = desc.label + " (Scratch)",
        .size = scratchSize,
        .usage = IssouRHI::BufferUsage::Storage,
    };
    m_ScratchBuffer = ToBackend(m_Device)->CreateBuffer(scratchDesc);
  }

  {
    IssouRHI::BufferDesc resultDesc{
        .label = desc.label + " (Result)",
        .size = m_PrebuildInfo.ResultDataMaxSizeInBytes,
        .usage = IssouRHI::BufferUsage::Storage | IssouRHI::BufferUsage::RayTracingAccelerationStructure,
    };
    m_Buffer = ToBackend(m_Device)->CreateBuffer(resultDesc);
  }

  {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure = {
        .Location = m_Buffer->GpuAddress(),
    };

    m_Srv = ToBackend(m_Device)->AllocCbvSrvUavDescriptor();
    ToBackend(m_Device)->GetNativeDevice()->CreateShaderResourceView(nullptr, &srvDesc, m_Srv.cpuHandle);
  }
}
}  // namespace D3D12
}  // namespace IssouRHI
