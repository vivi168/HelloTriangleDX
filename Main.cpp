#include "stdafx.h"
#include "Win32Application.h"
#include "Renderer.h"

static void PrintHelp()
{
  wprintf(
      L"Command line syntax:\n"
      L"-h, --Help   Print this information\n"
      L"-l, --List   Print list of GPUs\n"
      L"-g S, --GPU S   Select GPU with name containing S\n"
      L"-i N, --GPUIndex N   Select GPU index N\n");
}

struct GPUSelection {
  UINT32 Index = UINT32_MAX;
  std::wstring Substring;
};

class DXGIUsage
{
public:
  DXGIUsage() { CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&m_DXGIFactory))); }

  IDXGIFactory4* GetDXGIFactory() const { return m_DXGIFactory; }

  void PrintAdapterList() const
  {
    UINT index = 0;
    IDXGIAdapter1* adapter;

    while (m_DXGIFactory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
      DXGI_ADAPTER_DESC1 desc;
      adapter->GetDesc1(&desc);

      const bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
      const wchar_t* const suffix = isSoftware ? L" (SOFTWARE)" : L"";
      wprintf(L"Adapter %u: %s%s\n", index, desc.Description, suffix);

      index++;
    }
  }
  // If failed, returns null pointer.
  IDXGIAdapter1* CreateAdapter(const GPUSelection& gpuSelection) const
  {
    IDXGIAdapter1* adapter = NULL;

    if (gpuSelection.Index != UINT32_MAX) {
      // Cannot specify both index and name.
      if (!gpuSelection.Substring.empty()) {
        return NULL;
      }

      CHECK_HR(m_DXGIFactory->EnumAdapters1(gpuSelection.Index, &adapter));
      return adapter;
    }

    if (!gpuSelection.Substring.empty()) {
      IDXGIAdapter1* tmpAdapter;

      for (UINT i = 0; m_DXGIFactory->EnumAdapters1(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 desc;
        tmpAdapter->GetDesc1(&desc);

        if (StrStrI(desc.Description, gpuSelection.Substring.c_str())) {
          // Second matching adapter found - error.
          if (adapter) {
            return NULL;
          }

          // First matching adapter found.
          adapter = tmpAdapter;
        }
      }

      // Found or not, return it.
      return adapter;
    }

    // Select first one.
    m_DXGIFactory->EnumAdapters1(0, &adapter);
    return adapter;
  }

private:
  IDXGIFactory4* m_DXGIFactory;
};

struct CommandLineParameters {
  bool m_Help = false;
  bool m_List = false;
  GPUSelection m_GPUSelection;

  bool Parse(int argc, wchar_t** argv)
  {
    for (int i = 1; i < argc; ++i) {
      if (_wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"--Help") == 0) {
        m_Help = true;
      } else if (_wcsicmp(argv[i], L"-l") == 0 || _wcsicmp(argv[i], L"--List") == 0) {
        m_List = true;
      } else if ((_wcsicmp(argv[i], L"-g") == 0 || _wcsicmp(argv[i], L"--GPU") == 0) && i + 1 < argc) {
        m_GPUSelection.Substring = argv[++i];
      } else if ((_wcsicmp(argv[i], L"-i") == 0 || _wcsicmp(argv[i], L"--GPUIndex") == 0) && i + 1 < argc) {
        m_GPUSelection.Index = _wtoi(argv[++i]);
      } else {
        return false;
      }
    }
    return true;
  }
} g_CommandLineParameters;

enum class ExitCode : int {
  GPUList = 2,
  Help = 1,
  Success = 0,
  RuntimeError = -1,
  CommandLineError = -2,
};

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR,
                                          int nCmdShow)
{
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

  if (!g_CommandLineParameters.Parse(argc, argv)) {
    wprintf(L"ERROR: Invalid command line syntax.\n");
    PrintHelp();
    return (int)ExitCode::CommandLineError;
  }

  if (g_CommandLineParameters.m_Help) {
    PrintHelp();
    return (int)ExitCode::Help;
  }

  DXGIUsage dxgiUsage;

  if (g_CommandLineParameters.m_List) {
    dxgiUsage.PrintAdapterList();
    return (int)ExitCode::GPUList;
  }

  IDXGIAdapter1* adapter = dxgiUsage.CreateAdapter(g_CommandLineParameters.m_GPUSelection);
  Renderer::InitAdapter(dxgiUsage.GetDXGIFactory(), adapter);
  return Win32Application::Run(hInstance, nCmdShow);
}

int main(int argc, char** argv)
{
  return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}
