#include "IssouRHI.h"

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{

Surface::Surface(Device* device, HWND hwnd) : m_Device(device), m_Handle(hwnd)
{
  // Fence
  {
    ID3D12Fence* fence = nullptr;
    CHECK_HR(device->GetNativeDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    m_Fence.Attach(fence);

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_FenceEvent);
  }
}

Surface::~Surface()
{
  CloseHandle(m_FenceEvent);
}

void Surface::Configure(SurfaceConfiguration& config)
{
  CreateSwapChain(config);
  CreateTextures(config);
  m_EnableVsync = config.enableVsync;

  m_Configured = true;
}

void Surface::CreateSwapChain(SurfaceConfiguration& config)
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
  swapChainDesc.OutputWindow = m_Handle;

  swapChainDesc.SampleDesc.Count = 1;  // our multi-sampling description
  swapChainDesc.SampleDesc.Quality = 0;

  swapChainDesc.Windowed = true;

  ComPtr<IDXGIFactory4> dxgiFactory;
  CHECK_HR(m_Device->GetAdapter()->GetParent(IID_PPV_ARGS(&dxgiFactory)));

  IDXGISwapChain* swapChain = nullptr;
  CHECK_HR(dxgiFactory->CreateSwapChain(m_Device->GetQueue()->GetNativeQueue(), &swapChainDesc, &swapChain));
  m_SwapChain.Attach(static_cast<IDXGISwapChain3*>(swapChain));

  m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void Surface::CreateTextures(SurfaceConfiguration& config)
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

    m_Textures[i] = std::make_shared<Texture>(m_Device, desc);
    m_Textures[i]->Attach(res);

    m_FenceValues[i] = 0;
  }
}

std::shared_ptr<Texture> Surface::GetCurrentTexture()
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

void Surface::Present()
{
  CHECK_HR(m_SwapChain->Present(m_EnableVsync ? 1 : 0, 0));

  CHECK_HR(m_Device->GetQueue()->GetNativeQueue()->Signal(m_Fence.Get(), ++m_NextFenceValue));
  m_FenceValues[m_FrameIndex] = m_NextFenceValue;
}

}
