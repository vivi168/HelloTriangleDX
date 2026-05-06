#pragma once

#include <stdexcept>

#include <d3dx12.h>
#include <dxgi1_6.h>
#include <wrl.h>

#include <D3D12MemAlloc.h>

#include <IssouRHI.h>

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
// TODO: return std::expected for function that call CHECK_HR?
#define CHECK_HR(expr)                                                             \
  do {                                                                             \
    if (FAILED(expr)) {                                                            \
      assert(0 && #expr);                                                          \
      throw std::runtime_error(__FILE__ "(" LINE_STRING "): FAILED( " #expr " )"); \
    }                                                                              \
  } while (false)
