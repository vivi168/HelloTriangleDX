#include "UtilsD3D12.h"

#include <dxgidebug.h>

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{
namespace D3D12
{
std::wstring StringToWstring(std::string_view s)
{
  size_t len = s.length();

  if (len == 0) return std::wstring();

  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), len, nullptr, 0);
  assert(size > 0);

  std::wstring ws(size, 0);
  int res = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), len, ws.data(), size);
  assert(res > 0);

  return ws;
}

void ReportLiveObjects()
{
#ifdef ENABLE_DEBUG_LAYER
  {
    Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
      dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
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
}  // namespace D3D12
}  // namespace IssouRHI
