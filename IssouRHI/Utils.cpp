#include "IssouRHI.h"

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{

static ComPtr<IDXGIFactory4> CreateDXGIFactory()
{
  UINT dxgiFactoryFlags = 0;

#ifdef ENABLE_DEBUG_LAYER
  ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();

    // Enable additional debug layers.
    dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif

  ComPtr<IDXGIFactory4> factory;
  SUCCEEDED(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  return factory;
}

ComPtr<IDXGIFactory4> GetDXGIFactory()
{
  static ComPtr<IDXGIFactory4> factory = CreateDXGIFactory();

  return factory;
}

void PrintAdapterList()
{
  UINT index = 0;
  ComPtr<IDXGIAdapter1> adapter;
  while (GetDXGIFactory()->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    const bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    const wchar_t* const suffix = isSoftware ? L" (SOFTWARE)" : L"";
    wprintf(L"Adapter %u: %s%s\n", index, desc.Description, suffix);

    adapter.Reset();
    ++index;
  }
}

}  // namespace IssouRHI
