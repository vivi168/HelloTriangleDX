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

#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>

#include "D3D12MemAlloc.h"
#include "d3dx12_root_signature.h"
#include "d3dx12_resource_helpers.h"
#include "d3dx12_barriers.h"

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
#include <string>

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "RendererHelper.h"
