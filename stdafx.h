// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently.

#pragma once

#include <windows.h>

#include <shellapi.h>
#include <shlwapi.h>
#include <wrl.h>

#include <dxgi1_6.h>
#include <initguid.h>

#include "d3dx12_barriers.h"
#include "d3dx12_pipeline_state_stream.h"
#include "d3dx12_resource_helpers.h"
#include "d3dx12_root_signature.h"

#include <DirectXMath.h>
#include <DirectXMesh.h>
#include <DirectXTex.h>

#include "D3D12MemAlloc.h"

#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <list>
#include <memory>
#include <numeric>
#include <optional>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

static_assert(sizeof(WORD) == 2);
static_assert(sizeof(DWORD) == 4);
static_assert(sizeof(UINT) == 4);

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
#define CHECK_HR(expr)                                                             \
  do {                                                                             \
    if (FAILED(expr)) {                                                            \
      assert(0 && #expr);                                                          \
      throw std::runtime_error(__FILE__ "(" LINE_STRING "): FAILED( " #expr " )"); \
    }                                                                              \
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

// UAV counter must be aligned on 4K boundaries
inline constexpr UINT AlignForUavCounter(UINT bufferSize)
{
  const UINT alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
  return (bufferSize + (alignment - 1)) & ~(alignment - 1);
}

#define SizeOfInUint(x) ((sizeof(x) - 1) / sizeof(UINT) + 1)
