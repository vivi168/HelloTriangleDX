// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used stuff from Windows headers.
#endif
#define NOMINMAX

#include <windows.h>

#include <wrl.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <initguid.h>
#include <d3d12.h> // TODO: this is in fact coming DirectX-Headers.
#include <dxgi1_6.h>
#include <DirectXMath.h>

#include "D3D12MemAlloc.h"
#include "d3dx12_root_signature.h"
#include "d3dx12_resource_helpers.h"
#include "d3dx12_barriers.h"
#include "d3dx12_pipeline_state_stream.h"

#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include <iostream>
#include <fstream>
#include <list>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <numeric>
#include <array>
#include <type_traits>
#include <utility>
#include <chrono>
#include <string>
#include <exception>
#include <stdexcept>
#include <deque>
#include <optional>

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

static_assert(sizeof(WORD) == 2);
static_assert(sizeof(DWORD) == 4);
static_assert(sizeof(UINT) == 4);

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
#define CHECK_HR(expr)                                                      \
  do {                                                                      \
    if (FAILED(expr)) {                                                     \
      assert(0 && #expr);                                                   \
      throw std::runtime_error(__FILE__ "(" LINE_STRING "): FAILED( " #expr \
                                        " )");                              \
    }                                                                       \
  } while (false)

template <typename T, typename U>
inline constexpr T DivRoundUp(T num, U denom)
{
  return (num + denom - 1) / denom;
}

template <typename T, typename U>
inline constexpr T AlignUp(T val, U align)
{
  return DivRoundUp(val, align) * align;
}
