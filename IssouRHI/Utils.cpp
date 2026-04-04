#include "IssouRHI.h"

#include <dxgidebug.h>

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{
void ReportLiveObjects()
{
#ifdef ENABLE_DEBUG_LAYER
  {
    Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
      dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL,
                                   DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    }
  }
#endif
}

void PrintAdapterList()
{
  UINT index = 0;
  ComPtr<IDXGIFactory4> dxgiFactory;
  ComPtr<IDXGIAdapter1> adapter;

  CHECK_HR(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)));

  while (dxgiFactory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
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
