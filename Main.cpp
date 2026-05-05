#include "InteropD3D12.h"
#include "IssouRHI.h"
#include "Win32Application.h"
#include "stdafx.h"

#include <shellapi.h>

static void PrintHelp()
{
  printf(
      "Command line syntax:\n"
      "-h, --Help   Print this information\n"
      "-l, --List   Print list of GPUs\n"
      "-g S, --GPU S   Select GPU with name containing S\n"
      "-i N, --GPUIndex N   Select GPU index N\n");
}

struct CommandLineParameters {
  bool m_Help = false;
  bool m_List = false;
  IssouRHI::GPUSelection m_GPUSelection;

  bool Parse(int argc, char** argv)
  {
    for (int i = 1; i < argc; ++i) {
      if (_stricmp(argv[i], "-h") == 0 || _stricmp(argv[i], "--Help") == 0) {
        m_Help = true;
      } else if (_stricmp(argv[i], "-l") == 0 || _stricmp(argv[i], "--List") == 0) {
        m_List = true;
      } else if ((_stricmp(argv[i], "-g") == 0 || _stricmp(argv[i], "--GPU") == 0) && i + 1 < argc) {
        m_GPUSelection.substring = argv[++i];
      } else if ((_stricmp(argv[i], "-i") == 0 || _stricmp(argv[i], "--GPUIndex") == 0) && i + 1 < argc) {
        m_GPUSelection.index = atoi(argv[++i]);
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

int main(int argc, char** argv)
{
    if (!g_CommandLineParameters.Parse(argc, argv)) {
    wprintf(L"ERROR: Invalid command line syntax.\n");
    PrintHelp();
    return (int)ExitCode::CommandLineError;
  }

  if (g_CommandLineParameters.m_Help) {
    PrintHelp();
    return (int)ExitCode::Help;
  }

  if (g_CommandLineParameters.m_List) {
#ifdef BUILD_D3D12_BACKEND
    IssouRHI::D3D12::PrintAdapterList();
#endif
    return (int)ExitCode::GPUList;
  }

  auto device = IssouRHI::Device::CreateDevice(IssouRHI::Backend::D3D12, g_CommandLineParameters.m_GPUSelection);
  auto result = Win32Application::Run(GetModuleHandle(NULL), SW_SHOW, std::move(device));

  return result;
}
