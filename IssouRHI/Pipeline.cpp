#include "IssouRHI.h"
#include "Utils.h"

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

ComputePipeline::ComputePipeline(Device* device, const ComputePipelineDesc& desc) : PipelineBase(device), m_Desc(desc) {}

ComputePipeline::~ComputePipeline() = default;

void ComputePipeline::Create()
{
  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = m_Device->RootSignature();

  assert(m_Desc.shader.stage == ShaderStage::Compute);
  psoDesc.CS = D3D12ShaderByteCode(m_Desc.shader);

  ID3D12PipelineState* pipelineStateObject;
  CHECK_HR(m_Device->GetNativeDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));

  m_Pso.Attach(pipelineStateObject);
}

GraphicPipeline::GraphicPipeline(Device* device, const GraphicPipelineDesc& desc, Type type) : PipelineBase(device), m_Desc(desc), m_Type(type) {}

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

void GraphicPipeline::Create()
{
  const auto fillPsoDesc = [&](auto& psoDesc) {
    psoDesc.pRootSignature = m_Device->RootSignature();

    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.BlendState.AlphaToCoverageEnable = m_Desc.multiSample.alphaToCoverageEnabled;
    psoDesc.BlendState.IndependentBlendEnable = TRUE;

    psoDesc.SampleMask = m_Desc.multiSample.mask;

    // RasterizerState
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12CullMode(m_Desc.primitive.cullMode);
    psoDesc.RasterizerState.FrontCounterClockwise = m_Desc.primitive.frontFace == FrontFace::CCW;
    psoDesc.RasterizerState.DepthBias = m_Desc.depthStencil.depthBias;
    psoDesc.RasterizerState.DepthBiasClamp = m_Desc.depthStencil.depthBiasClamp;
    psoDesc.RasterizerState.SlopeScaledDepthBias = m_Desc.depthStencil.depthBiasSlopeScale;
    psoDesc.RasterizerState.DepthClipEnable = !m_Desc.primitive.unclippedDepth;
    psoDesc.RasterizerState.MultisampleEnable = m_Desc.multiSample.count > 1;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
    psoDesc.RasterizerState.ForcedSampleCount = 0;
    psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // DepthStencilState
    psoDesc.DepthStencilState.DepthEnable = m_Desc.depthStencil.depthWriteEnabled || m_Desc.depthStencil.depthCompare != CompareFunction::Always;
    psoDesc.DepthStencilState.DepthWriteMask = m_Desc.depthStencil.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = D3D12ComparisonFunc(m_Desc.depthStencil.depthCompare);

    psoDesc.DepthStencilState.StencilEnable = m_Desc.depthStencil.stencilFront.Enabled() || m_Desc.depthStencil.stencilBack.Enabled();
    psoDesc.DepthStencilState.StencilReadMask = static_cast<UINT8>(m_Desc.depthStencil.stencilReadMask);
    psoDesc.DepthStencilState.StencilWriteMask = static_cast<UINT8>(m_Desc.depthStencil.stencilWriteMask);
    psoDesc.DepthStencilState.FrontFace = D3D12DepthStencilOpDesc(m_Desc.depthStencil.stencilFront);
    psoDesc.DepthStencilState.BackFace = D3D12DepthStencilOpDesc(m_Desc.depthStencil.stencilBack);

    psoDesc.PrimitiveTopologyType = D3D12PrimitiveTopologyType(m_Desc.primitive.topology);

    psoDesc.DSVFormat = DXGIFormat(m_Desc.depthStencil.format);
    assert(!(psoDesc.DepthStencilState.DepthEnable || psoDesc.DepthStencilState.StencilEnable) || psoDesc.DSVFormat != DXGI_FORMAT_UNKNOWN);

    psoDesc.SampleDesc = {
        .Count = m_Desc.multiSample.count,
        .Quality = 0,
    };

    assert(m_Desc.targets.size() <= D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
    psoDesc.NumRenderTargets = m_Desc.targets.size();

    for (size_t i = 0; i < psoDesc.NumRenderTargets; i++) {
      auto& target = m_Desc.targets[i];
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

      for (const auto& shader : m_Desc.shaders) {
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

      psoDesc.IBStripCutValue = D3D12IndexBufferStripCutValue(m_Desc.primitive.topology, m_Desc.primitive.stripIndexFormat);

      CHECK_HR(m_Device->GetNativeDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
      break;
    }
    case Type::Mesh: {
      D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
      fillPsoDesc(psoDesc);

      for (const auto& shader : m_Desc.shaders) {
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
  m_PrimitiveTopology = D3D12PrimitiveTopology(m_Desc.primitive.topology);
}

RenderPipeline::RenderPipeline(Device* device, const GraphicPipelineDesc& desc) : GraphicPipeline(device, desc, GraphicPipeline::Type::Render) {}

RenderPipeline::~RenderPipeline() = default;

MeshPipeline::MeshPipeline(Device* device, const GraphicPipelineDesc& desc) : GraphicPipeline(device, desc, GraphicPipeline::Type::Mesh) {}

MeshPipeline::~MeshPipeline() = default;

}  // namespace IssouRHI
