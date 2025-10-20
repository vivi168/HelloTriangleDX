#include "IssouRHI.h"
#include "Utils.h"

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{

// Surface::Surface(std::shared_ptr<Device> device, HWND hwnd)
// {
//   m_Device = device;
//   m_CommandQueue = device->GetCommandQueue();
//   m_Handle = hwnd;
//   // create fence
// }

// void Surface::Configure(SurfaceConfiguration& desc)
// {
//   CreateSwapChain(SurfaceConfiguration& desc);
//   CreateTextures(SurfaceConfiguration& desc);

//   m_Configured = true;
// }

// void Surface::CreateSwapChain(SurfaceConfiguration& desc)
// {
//   if (m_Configured) return;

//   // this is to describe our display mode
//   DXGI_MODE_DESC backBufferDesc = {};
//   backBufferDesc.Width = desc.width;
//   backBufferDesc.Height = desc.height;
//   backBufferDesc.Format = desc.format;

//   // Describe and create the swap chain.
//   DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
//   swapChainDesc.BufferCount = desc.bufferCount;
//   swapChainDesc.BufferDesc = backBufferDesc;
//   // this says the pipeline will render to this swap chain
//   swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
//   // dxgi will discard the buffer (data) after we call present
//   swapChainDesc.SwapEffect = desc.swapEffect;
//   swapChainDesc.OutputWindow = m_Handle;

//   swapChainDesc.SampleDesc.Count = 1;  // our multi-sampling description
//   swapChainDesc.SampleDesc.Quality = 0;

//   swapChainDesc.Windowed = true;  // set to true, then if in fullscreen must
//                                   // call SetFullScreenState with true for
//                                   // full screen to get uncapped fps

//   IDXGISwapChain* swapChain = nullptr;
//   CHECK_HR(g_Factory->CreateSwapChain(g_CommandQueue.Get(), &swapChainDesc, &swapChain));
//   m_SwapChain.Attach(static_cast<IDXGISwapChain3*>(swapChain));

//   m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
// }

// void Surface::CreateTextures(SurfaceConfiguration& desc)
// {
//   if (m_Configured) return;

//   m_Textures.reserve(desc.bufferCount);

//   for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
//     ID3D12Resource* res = nullptr;
//     CHECK_HR(g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&res)));


//     // also set a TextureDesc, and pass it to Texture.
//     // m_Textures[i] = std::make_shared<Texture>(desc);
//     // m_Textures[i].Attach(res);
//   }
// }

}