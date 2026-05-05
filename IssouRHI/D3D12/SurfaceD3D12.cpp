#include "SurfaceD3D12.h"

#include "DeviceD3D12.h"
#include "QueueD3D12.h"
#include "TextureD3D12.h"
#include "UtilsD3D12.h"

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{
namespace D3D12
{
SurfaceImpl::SurfaceImpl(Device* device, void* handle) : Surface(device, handle) {}

SurfaceImpl::~SurfaceImpl()
{
  CloseHandle(m_FenceEvent);
}

void SurfaceImpl::Create()
{
  // Fence
  {
    ID3D12Fence* fence = nullptr;
    CHECK_HR(ToBackend(m_Device)->GetNativeDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    m_Fence.Attach(fence);

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_FenceEvent);
  }
}

void SurfaceImpl::CreateSwapChain(SurfaceConfiguration& config)
{
  if (m_Configured) return;

  // this is to describe our display mode
  DXGI_MODE_DESC backBufferDesc = {};
  backBufferDesc.Width = config.width;
  backBufferDesc.Height = config.height;
  backBufferDesc.Format = DXGIFormat(config.format);

  // Describe and create the swap chain.
  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  swapChainDesc.BufferCount = config.bufferCount;
  swapChainDesc.BufferDesc = backBufferDesc;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.OutputWindow = reinterpret_cast<HWND>(m_Handle);

  swapChainDesc.SampleDesc.Count = 1;  // our multi-sampling description
  swapChainDesc.SampleDesc.Quality = 0;

  swapChainDesc.Windowed = true;

  ComPtr<IDXGIFactory4> dxgiFactory;
  CHECK_HR(ToBackend(m_Device)->GetAdapter()->GetParent(IID_PPV_ARGS(&dxgiFactory)));

  IDXGISwapChain* swapChain = nullptr;
  CHECK_HR(dxgiFactory->CreateSwapChain(ToBackend(m_Device->GetQueue())->GetNativeQueue(), &swapChainDesc, &swapChain));
  m_SwapChain.Attach(static_cast<IDXGISwapChain3*>(swapChain));

  m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void SurfaceImpl::CreateTextures(SurfaceConfiguration& config)
{
  if (m_Configured) return;

  m_Textures.resize(config.bufferCount);
  m_FenceValues.resize(config.bufferCount);

  TextureDesc desc{};
  desc.size = {
      .width = config.width,
      .height = config.height,
  };
  desc.format = config.format;
  desc.usage = TextureUsage::RenderAttachment;

  for (UINT i = 0; i < config.bufferCount; i++) {
    ID3D12Resource* res = nullptr;
    CHECK_HR(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&res)));

    auto tex = std::make_shared<TextureImpl>(m_Device, desc);
    tex->Attach(res);

    m_Textures[i] = tex;

    m_FenceValues[i] = 0;
  }
}

std::shared_ptr<Texture> SurfaceImpl::GetCurrentTexture()
{
  m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
  auto texture = m_Textures[m_FrameIndex];

  UINT64 fenceValue = m_FenceValues[m_FrameIndex];
  if (m_Fence->GetCompletedValue() < fenceValue) {
    CHECK_HR(m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent));
    WaitForSingleObject(m_FenceEvent, INFINITE);
  }

  return texture;
}

void SurfaceImpl::Present()
{
  CHECK_HR(m_SwapChain->Present(m_EnableVsync ? 1 : 0, 0));

  CHECK_HR(ToBackend(m_Device->GetQueue())->GetNativeQueue()->Signal(m_Fence.Get(), ++m_NextFenceValue));
  m_FenceValues[m_FrameIndex] = m_NextFenceValue;
}
}
}
