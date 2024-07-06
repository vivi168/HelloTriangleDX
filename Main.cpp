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

struct CommandLineParameters {
  bool m_Help = false;
  bool m_List = false;
  GPUSelection m_GPUSelection;

  bool Parse(int argc, wchar_t** argv)
  {
    for (int i = 1; i < argc; ++i) {
      if (_wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"--Help") == 0) {
        m_Help = true;
      } else if (_wcsicmp(argv[i], L"-l") == 0 ||
                 _wcsicmp(argv[i], L"--List") == 0) {
        m_List = true;
      } else if ((_wcsicmp(argv[i], L"-g") == 0 ||
                  _wcsicmp(argv[i], L"--GPU") == 0) &&
                 i + 1 < argc) {
        m_GPUSelection.Substring = argv[i + 1];
        ++i;
      } else if ((_wcsicmp(argv[i], L"-i") == 0 ||
                  _wcsicmp(argv[i], L"--GPUIndex") == 0) &&
                 i + 1 < argc) {
        m_GPUSelection.Index = _wtoi(argv[i + 1]);
        ++i;
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

  // variable name should not same as class name
  DXGIUsage dxgiUsage;
  dxgiUsage.Init();

  if (g_CommandLineParameters.m_List) {
    dxgiUsage.PrintAdapterList();
    return (int)ExitCode::GPUList;
  }

  Renderer::InitAdapter(&dxgiUsage,
                        g_CommandLineParameters.m_GPUSelection);
  return Win32Application::Run(hInstance, nCmdShow);
}

int main(int argc, char** argv)
{
  return WinMain(GetModuleHandle(NULL), NULL, GetCommandLineA(), SW_SHOW);
}
