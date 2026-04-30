#include "IssouRHI.h"

namespace IssouRHI
{
ShaderTable::ShaderTable(Device* device) : m_Device(device) {}

ShaderTable::~ShaderTable() = default;

static constexpr uint32_t RecordAlignment = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;

void ShaderTable::Create(const ShaderTableDesc& desc)
{
  static constexpr uint32_t TableAlignment = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

  m_RayGenShaderRecordSize = TableAlignment;
  m_MissShaderTableSize = desc.missEntryPoints.size() * RecordAlignment;
  m_HitGroupTableSize = desc.hitGroupNames.size() * RecordAlignment;
  m_CallableShaderTableSize = desc.callableEntryPoints.size() * RecordAlignment;

  m_MissShaderTableOffset = m_RayGenShaderRecordSize;
  m_HitGroupTableOffset = AlignUpPowerOfTwo(m_MissShaderTableOffset + m_MissShaderTableSize, TableAlignment);
  m_CallableShaderTableOffset = AlignUpPowerOfTwo(m_HitGroupTableOffset + m_HitGroupTableSize, TableAlignment);

  uint32_t tableSize = m_CallableShaderTableOffset + m_CallableShaderTableSize;

  BufferDesc bufferDesc{
      .label = desc.label,
      .size = tableSize,
      .usage = IssouRHI::BufferUsage::MapWrite,
  };
  m_Buffer = m_Device->CreateBuffer(bufferDesc);

  std::vector<std::byte> tableData(tableSize);

  auto writeShaderIdentifier = [&](void* dst, const std::string& entryPoint) {
    void* identifier = desc.pipeline->ShaderIdentifier(entryPoint);

    memcpy(dst, identifier, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
  };

  writeShaderIdentifier(tableData.data(), desc.rayGenEntryPoint);

  for (size_t i = 0; i < desc.missEntryPoints.size(); i++) {
    writeShaderIdentifier(tableData.data() + m_MissShaderTableOffset + i * RecordAlignment, desc.missEntryPoints[i]);
  }

  for (size_t i = 0; i < desc.hitGroupNames.size(); i++) {
    writeShaderIdentifier(tableData.data() + m_HitGroupTableOffset + i * RecordAlignment, desc.hitGroupNames[i]);
  }

  for (size_t i = 0; i < desc.callableEntryPoints.size(); i++) {
    writeShaderIdentifier(tableData.data() + m_CallableShaderTableOffset + i * RecordAlignment, desc.callableEntryPoints[i]);
  }

  m_Buffer->Write(FullBufferRange, tableData.data());
}

D3D12_GPU_VIRTUAL_ADDRESS_RANGE ShaderTable::RayGenShaderRecord() const
{
  return D3D12_GPU_VIRTUAL_ADDRESS_RANGE{
      .StartAddress = m_Buffer->GpuAddress(),
      .SizeInBytes = m_RayGenShaderRecordSize,
  };
}

D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE ShaderTable::MissShaderTable() const
{
  if (m_MissShaderTableSize == 0) {
    return D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{};
  }

  return D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{
      .StartAddress = m_Buffer->GpuAddress() + m_MissShaderTableOffset,
      .SizeInBytes = m_MissShaderTableSize,
      .StrideInBytes = RecordAlignment,
  };
}

D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE ShaderTable::HitGroupTable() const
{
  if (m_HitGroupTableSize == 0) {
    return D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{};
  }

  return D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{
      .StartAddress = m_Buffer->GpuAddress() + m_HitGroupTableOffset,
      .SizeInBytes = m_HitGroupTableSize,
      .StrideInBytes = RecordAlignment,
  };
}

D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE ShaderTable::CallableShaderTable() const
{
  if (m_CallableShaderTableSize == 0) {
    return D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{};
  }

  return D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE{
      .StartAddress = m_Buffer->GpuAddress() + m_CallableShaderTableOffset,
      .SizeInBytes = m_CallableShaderTableSize,
      .StrideInBytes = RecordAlignment,
  };
}
}  // namespace IssouRHI
