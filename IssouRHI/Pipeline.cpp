#include "IssouRHI.h"

namespace IssouRHI
{
PipelineBase::PipelineBase(Device* device) : m_Device(device) {}

PipelineBase::~PipelineBase() = default;

static D3D12_SHADER_BYTECODE D3D12ShaderByteCode(const ShaderModule& shader)
{
  return D3D12_SHADER_BYTECODE{
      .pShaderBytecode = shader.code,
      .BytecodeLength = shader.size,
  };
}

ComputePipeline::ComputePipeline(Device* device) : PipelineBase(device) {}

ComputePipeline::~ComputePipeline() = default;

void ComputePipeline::Create(const ComputePipelineDesc& desc)
{
  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = m_Device->RootSignature();

  assert(desc.shader.stage == ShaderStage::Compute);
  psoDesc.CS = D3D12ShaderByteCode(desc.shader);

  ID3D12PipelineState* pipelineStateObject;
  CHECK_HR(m_Device->GetNativeDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));

  m_Pso.Attach(pipelineStateObject);
  m_Pso->SetName(StringToWstring(desc.label).c_str());
}

GraphicPipeline::GraphicPipeline(Device* device, Type type) : PipelineBase(device), m_Type(type) {}

GraphicPipeline::~GraphicPipeline() = default;

static D3D12_CULL_MODE D3D12CullMode(CullMode mode)
{
  switch (mode) {
    case CullMode::None:
      return D3D12_CULL_MODE_NONE;
    case CullMode::Front:
      return D3D12_CULL_MODE_FRONT;
    case CullMode::Back:
      return D3D12_CULL_MODE_BACK;
    default:
      std::unreachable();
  }
}

static D3D12_COMPARISON_FUNC D3D12ComparisonFunc(CompareFunction function)
{
  switch (function) {
    case CompareFunction::Never:
      return D3D12_COMPARISON_FUNC_NEVER;
    case CompareFunction::Less:
      return D3D12_COMPARISON_FUNC_LESS;
    case CompareFunction::Equal:
      return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareFunction::LessEqual:
      return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareFunction::Greater:
      return D3D12_COMPARISON_FUNC_GREATER;
    case CompareFunction::NotEqual:
      return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareFunction::GreaterEqual:
      return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareFunction::Always:
      return D3D12_COMPARISON_FUNC_ALWAYS;
    default:
      std::unreachable();
  }
}

static D3D12_STENCIL_OP D3D12DepthStencilOp(StencilOperation op)
{
  switch (op) {
    case StencilOperation::Keep:
      return D3D12_STENCIL_OP_KEEP;
    case StencilOperation::Zero:
      return D3D12_STENCIL_OP_ZERO;
    case StencilOperation::Replace:
      return D3D12_STENCIL_OP_REPLACE;
    case StencilOperation::Invert:
      return D3D12_STENCIL_OP_INVERT;
    case StencilOperation::IncrementClamp:
      return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOperation::DecrementClamp:
      return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOperation::IncrementWrap:
      return D3D12_STENCIL_OP_INCR;
    case StencilOperation::DecrementWrap:
      return D3D12_STENCIL_OP_DECR;
    default:
      std::unreachable();
  }
}

static D3D12_DEPTH_STENCILOP_DESC D3D12DepthStencilOpDesc(const StencilFaceState& state)
{
  return D3D12_DEPTH_STENCILOP_DESC{
      .StencilFailOp = D3D12DepthStencilOp(state.failOp),
      .StencilDepthFailOp = D3D12DepthStencilOp(state.depthFailOp),
      .StencilPassOp = D3D12DepthStencilOp(state.passOp),
      .StencilFunc = D3D12ComparisonFunc(state.compare),
  };
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE D3D12PrimitiveTopologyType(PrimitiveTopology topology)
{
  switch (topology) {
    case PrimitiveTopology::PointList:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    default:
      std::unreachable();
  }
}

static D3D12_PRIMITIVE_TOPOLOGY D3D12PrimitiveTopology(PrimitiveTopology topology)
{
  switch (topology) {
    case PrimitiveTopology::PointList:
      return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList:
      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip:
      return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip:
      return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default:
      std::unreachable();
  }
}

static D3D12_BLEND D3D12Blend(BlendFactor factor)
{
  switch (factor) {
    case BlendFactor::Zero:
      return D3D12_BLEND_ZERO;
    case BlendFactor::One:
      return D3D12_BLEND_ONE;
    case BlendFactor::Src:
      return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::OneMinusSrc:
      return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha:
      return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
      return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::Dst:
      return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::OneMinusDst:
      return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::DstAlpha:
      return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDstAlpha:
      return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::SrcAlphaSaturated:
      return D3D12_BLEND_SRC_ALPHA_SAT;
    case BlendFactor::Constant:
      return D3D12_BLEND_BLEND_FACTOR;
    case BlendFactor::OneMinusConstant:
      return D3D12_BLEND_INV_BLEND_FACTOR;
    case BlendFactor::Src1:
      return D3D12_BLEND_SRC1_COLOR;
    case BlendFactor::OneMinusSrc1:
      return D3D12_BLEND_INV_SRC1_COLOR;
    case BlendFactor::Src1Alpha:
      return D3D12_BLEND_SRC1_ALPHA;
    case BlendFactor::OneMinusSrc1Alpha:
      return D3D12_BLEND_INV_SRC1_ALPHA;
    default:
      std::unreachable();
  }
}

static D3D12_BLEND D3D12BlendAlpha(BlendFactor factor)
{
  switch (factor) {
    case BlendFactor::Src:
      return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSrc:
      return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::Dst:
      return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDst:
      return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::Src1:
      return D3D12_BLEND_SRC1_ALPHA;
    case BlendFactor::OneMinusSrc1:
      return D3D12_BLEND_INV_SRC1_ALPHA;
    default:
      return D3D12Blend(factor);
  }
}

static D3D12_BLEND_OP D3D12BlendOperation(BlendOperation operation)
{
  switch (operation) {
    case BlendOperation::Add:
      return D3D12_BLEND_OP_ADD;
    case BlendOperation::Subtract:
      return D3D12_BLEND_OP_SUBTRACT;
    case BlendOperation::ReverseSubtract:
      return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOperation::Min:
      return D3D12_BLEND_OP_MIN;
    case BlendOperation::Max:
      return D3D12_BLEND_OP_MAX;
    default:
      std::unreachable();
  }
}

static D3D12_INDEX_BUFFER_STRIP_CUT_VALUE D3D12IndexBufferStripCutValue(PrimitiveTopology topology, IndexFormat indexFormat)
{
  if (topology != PrimitiveTopology::LineStrip && topology != PrimitiveTopology::TriangleStrip) {
    return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  }

  switch (indexFormat) {
    case IndexFormat::Undefined:
      return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    case IndexFormat::Uint16:
      return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFF;
    case IndexFormat::Uint32:
      return D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    default:
      std::unreachable();
  }
}

void GraphicPipeline::Create(const GraphicPipelineDesc& desc)
{
  const auto fillPsoDesc = [&](auto& psoDesc) {
    psoDesc.pRootSignature = m_Device->RootSignature();

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.AlphaToCoverageEnable = desc.multiSample.alphaToCoverageEnabled;
    psoDesc.BlendState.IndependentBlendEnable = TRUE;

    psoDesc.SampleMask = desc.multiSample.mask;

    // RasterizerState
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12CullMode(desc.primitive.cullMode);
    psoDesc.RasterizerState.FrontCounterClockwise = desc.primitive.frontFace == FrontFace::CCW;
    psoDesc.RasterizerState.DepthBias = desc.depthStencil.depthBias;
    psoDesc.RasterizerState.DepthBiasClamp = desc.depthStencil.depthBiasClamp;
    psoDesc.RasterizerState.SlopeScaledDepthBias = desc.depthStencil.depthBiasSlopeScale;
    psoDesc.RasterizerState.DepthClipEnable = !desc.primitive.unclippedDepth;
    psoDesc.RasterizerState.MultisampleEnable = desc.multiSample.count > 1;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // DepthStencilState
    psoDesc.DepthStencilState.DepthEnable = desc.depthStencil.depthWriteEnabled || desc.depthStencil.depthCompare != CompareFunction::Always;
    psoDesc.DepthStencilState.DepthWriteMask = desc.depthStencil.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12ComparisonFunc(desc.depthStencil.depthCompare);

    psoDesc.DepthStencilState.StencilEnable = desc.depthStencil.stencilFront.Enabled() || desc.depthStencil.stencilBack.Enabled();
    psoDesc.DepthStencilState.StencilReadMask = static_cast<UINT8>(desc.depthStencil.stencilReadMask);
    psoDesc.DepthStencilState.StencilWriteMask = static_cast<UINT8>(desc.depthStencil.stencilWriteMask);
    psoDesc.DepthStencilState.FrontFace = D3D12DepthStencilOpDesc(desc.depthStencil.stencilFront);
    psoDesc.DepthStencilState.BackFace = D3D12DepthStencilOpDesc(desc.depthStencil.stencilBack);

    psoDesc.PrimitiveTopologyType = D3D12PrimitiveTopologyType(desc.primitive.topology);

    psoDesc.DSVFormat = DXGIFormat(desc.depthStencil.format);
    assert(!(psoDesc.DepthStencilState.DepthEnable || psoDesc.DepthStencilState.StencilEnable) || psoDesc.DSVFormat != DXGI_FORMAT_UNKNOWN);

    psoDesc.SampleDesc = {
        .Count = desc.multiSample.count,
        .Quality = 0,
    };

    assert(desc.targets.size() <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
    psoDesc.NumRenderTargets = desc.targets.size();

    for (size_t i = 0; i < psoDesc.NumRenderTargets; i++) {
      auto& target = desc.targets[i];
      psoDesc.RTVFormats[i] = DXGIFormat(target.format);

      auto& blendDesc = psoDesc.BlendState.RenderTarget[i];
      blendDesc.BlendEnable = target.blend.has_value();
      if (blendDesc.BlendEnable) {
        blendDesc.SrcBlend = D3D12Blend(target.blend->color.srcFactor);
        blendDesc.SrcBlendAlpha = D3D12BlendAlpha(target.blend->alpha.srcFactor);

        blendDesc.DestBlend = D3D12Blend(target.blend->color.dstFactor);
        blendDesc.DestBlendAlpha = D3D12BlendAlpha(target.blend->alpha.dstFactor);

        blendDesc.BlendOp = D3D12BlendOperation(target.blend->color.operation);
        blendDesc.BlendOpAlpha = D3D12BlendOperation(target.blend->alpha.operation);
      }
      blendDesc.RenderTargetWriteMask = static_cast<uint8_t>(target.writeMask);
    }
  };

  ID3D12PipelineState* pipelineStateObject;

  switch (m_Type) {
    case Type::Render: {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
      fillPsoDesc(psoDesc);

      for (const auto& shader : desc.shaders) {
        if (shader.stage == ShaderStage::Fragment) {
          psoDesc.PS = D3D12ShaderByteCode(shader);
        } else if (shader.stage == ShaderStage::Vertex) {
          psoDesc.VS = D3D12ShaderByteCode(shader);
        }
      }

      psoDesc.InputLayout = {
          .pInputElementDescs = nullptr,
          .NumElements = 0,
      };

      psoDesc.IBStripCutValue = D3D12IndexBufferStripCutValue(desc.primitive.topology, desc.primitive.stripIndexFormat);

      CHECK_HR(m_Device->GetNativeDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
      break;
    }
    case Type::Mesh: {
      D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
      fillPsoDesc(psoDesc);

      for (const auto& shader : desc.shaders) {
        if (shader.stage == ShaderStage::Fragment) {
          psoDesc.PS = D3D12ShaderByteCode(shader);
        } else if (shader.stage == ShaderStage::Mesh) {
          psoDesc.MS = D3D12ShaderByteCode(shader);
        } else if (shader.stage == ShaderStage::Task) {
          psoDesc.AS = D3D12ShaderByteCode(shader);
        }
      }

      auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);
      D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
      streamDesc.pPipelineStateSubobjectStream = &psoStream;
      streamDesc.SizeInBytes = sizeof(psoStream);

      CHECK_HR(m_Device->GetNativeDevice()->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&pipelineStateObject)));
      break;
    }
    default:
      std::unreachable();
  }

  m_Pso.Attach(pipelineStateObject);
  m_Pso->SetName(StringToWstring(desc.label).c_str());
  m_PrimitiveTopology = D3D12PrimitiveTopology(desc.primitive.topology);
}

RenderPipeline::RenderPipeline(Device* device) : GraphicPipeline(device, GraphicPipeline::Type::Render) {}

RenderPipeline::~RenderPipeline() = default;

MeshPipeline::MeshPipeline(Device* device) : GraphicPipeline(device, GraphicPipeline::Type::Mesh) {}

MeshPipeline::~MeshPipeline() = default;

RayTracingPipeline::RayTracingPipeline(Device* device) : m_Device(device) {}

RayTracingPipeline::~RayTracingPipeline() = default;

static D3D12_RAYTRACING_PIPELINE_FLAGS D3D12RayTracingPipelineFlags(RayTracingPipelineFlags flags)
{
  D3D12_RAYTRACING_PIPELINE_FLAGS pipelineFlags = D3D12_RAYTRACING_PIPELINE_FLAG_NONE;

  if (flags & RayTracingPipelineFlags::SkipTriangles) {
    pipelineFlags |= D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES;
  }
  if (flags & RayTracingPipelineFlags::SkipAABBs) {
    pipelineFlags |= D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES;
  }
  if (flags & RayTracingPipelineFlags::AllowMicroMaps) {
    pipelineFlags |= D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS;
  }

  return pipelineFlags;
}

void RayTracingPipeline::Create(const RayTracingPipelineDesc& desc)
{
  CD3DX12_STATE_OBJECT_DESC raytracingPipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
  std::vector<D3D12_SHADER_BYTECODE> byteCodes(desc.shaders.size());

  for (size_t i = 0; i < desc.shaders.size(); ++i) {
    const auto& shader = desc.shaders[i];
    byteCodes[i] = D3D12ShaderByteCode(shader);

    auto lib = raytracingPipeline.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    lib->SetDXILLibrary(&byteCodes[i]);
    assert(shader.entryPointName.has_value());
    auto entryPoint = StringToWstring(shader.entryPointName.value());
    lib->DefineExport(entryPoint.c_str());
  }

  for (const auto& hitGroup : desc.hitGroups) {
    auto hitGroupSubobject = raytracingPipeline.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroupSubobject->SetHitGroupExport(StringToWstring(hitGroup.name).c_str());

    D3D12_HIT_GROUP_TYPE groupType = D3D12_HIT_GROUP_TYPE_TRIANGLES;

    if (hitGroup.anyHitEntryPoint.has_value()) {
      hitGroupSubobject->SetAnyHitShaderImport(StringToWstring(hitGroup.anyHitEntryPoint.value()).c_str());
    }
    if (hitGroup.closestHitEntryPoint.has_value()) {
      hitGroupSubobject->SetClosestHitShaderImport(StringToWstring(hitGroup.closestHitEntryPoint.value()).c_str());
    }
    if (hitGroup.intersectionEntryPoint.has_value()) {
      hitGroupSubobject->SetIntersectionShaderImport(StringToWstring(hitGroup.intersectionEntryPoint.value()).c_str());
      groupType = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
    }

    hitGroupSubobject->SetHitGroupType(groupType);
  }

  auto shaderConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
  shaderConfig->Config(desc.maxPayloadSize, desc.maxAttributeSize);

  auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
  globalRootSignature->SetRootSignature(m_Device->RootSignature());

  auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG1_SUBOBJECT>();
  pipelineConfig->Config(desc.maxRecursionDepth, D3D12RayTracingPipelineFlags(desc.flags));

  CHECK_HR(m_Device->GetNativeDevice()->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_StateObject)));

  m_StateObject->SetName(StringToWstring(desc.label).c_str());

// #ifdef _DEBUG
//     PrintStateObjectDesc(raytracingPipeline);
// #endif

  CHECK_HR(m_StateObject.As(&m_StateObjectProperties));
}

}  // namespace IssouRHI
