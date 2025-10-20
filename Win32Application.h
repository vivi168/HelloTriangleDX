#pragma once

#include "IssouRHI.h"

namespace Win32Application
{
int Run(HINSTANCE hInstance, int nCmdShow, std::shared_ptr<IssouRHI::Device> device);
HWND GetHwnd();
}  // namespace Win32Application
