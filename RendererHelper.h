#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>
#include <wrl.h>
#include <Shlwapi.h>

// Note that while ComPtr is used to manage the lifetime of resources on the
// CPU, it has no understanding of the lifetime of resources on the GPU. Apps
// must account for the GPU lifetime of resources to avoid destroying objects
// that may still be referenced by the GPU.
using Microsoft::WRL::ComPtr;

inline std::string HrToString(HRESULT hr)
{
  char s_str[64] = {};
  sprintf_s(s_str, "HRESULT of 0x%08X", static_cast<UINT>(hr));
  return std::string(s_str);
}

class HrException : public std::runtime_error
{
public:
  HrException(HRESULT hr) : std::runtime_error(HrToString(hr)), m_hr(hr) {}
  HRESULT Error() const { return m_hr; }

private:
  const HRESULT m_hr;
};

#define SAFE_RELEASE(p) \
  if (p) (p)->Release()

inline void ThrowIfFailed(HRESULT hr)
{
  if (FAILED(hr)) {
    throw HrException(hr);
  }
}

inline void GetAssetsPath(_Out_writes_(pathSize) WCHAR* path, UINT pathSize)
{
  if (path == nullptr) {
    throw std::exception();
  }

  DWORD size = GetModuleFileName(nullptr, path, pathSize);
  if (size == 0 || size == pathSize) {
    // Method failed or path was truncated.
    throw std::exception();
  }

  WCHAR* lastSlash = wcsrchr(path, L'\\');
  if (lastSlash) {
    *(lastSlash + 1) = L'\0';
  }
}

inline std::vector<uint8_t> ReadData(_In_z_ const wchar_t* name)
{
  std::ifstream inFile(name, std::ios::in | std::ios::binary | std::ios::ate);

#if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
  if (!inFile) {
    wchar_t moduleName[_MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, moduleName, _MAX_PATH))
      throw std::system_error(std::error_code(static_cast<int>(GetLastError()),
                                              std::system_category()),
                              "GetModuleFileNameW");

    wchar_t drive[_MAX_DRIVE];
    wchar_t path[_MAX_PATH];

    if (_wsplitpath_s(moduleName, drive, _MAX_DRIVE, path, _MAX_PATH, nullptr,
                      0, nullptr, 0))
      throw std::runtime_error("_wsplitpath_s");

    wchar_t filename[_MAX_PATH];
    if (_wmakepath_s(filename, _MAX_PATH, drive, path, name, nullptr))
      throw std::runtime_error("_wmakepath_s");

    inFile.open(filename, std::ios::in | std::ios::binary | std::ios::ate);
  }
#endif

  if (!inFile) throw std::runtime_error("ReadData");

  const std::streampos len = inFile.tellg();
  if (!inFile) throw std::runtime_error("ReadData");

  std::vector<uint8_t> blob;
  blob.resize(size_t(len));

  inFile.seekg(0, std::ios::beg);
  if (!inFile) throw std::runtime_error("ReadData");

  inFile.read(reinterpret_cast<char*>(blob.data()), len);
  if (!inFile) throw std::runtime_error("ReadData");

  inFile.close();

  return blob;
}

// Naming helper for ComPtr<T>.
// Assigns the name of the variable as the name of the object.
// The indexed variant will include the index in the name of the object.
#define NAME_D3D12_OBJECT(x) SetName((x).Get(), L#x)
#define NAME_D3D12_OBJECT_INDEXED(x, n) SetNameIndexed((x)[n].Get(), L#x, n)

// Resets all elements in a ComPtr array.
template <class T>
void ResetComPtrArray(T* comPtrArray)
{
  for (auto& i : *comPtrArray) {
    i.Reset();
  }
}

// Resets all elements in a unique_ptr array.
template <class T>
void ResetUniquePtrArray(T* uniquePtrArray)
{
  for (auto& i : *uniquePtrArray) {
    i.reset();
  }
}

typedef std::chrono::high_resolution_clock::time_point time_point;
typedef std::chrono::high_resolution_clock::duration duration;

#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STRING STRINGIZE(__LINE__)
#define CHECK_BOOL(expr)                                              \
  do {                                                                \
    if (!(expr)) {                                                    \
      assert(0 && #expr);                                             \
      throw std::runtime_error(__FILE__ "(" LINE_STRING "): ( " #expr \
                                        " ) == false");               \
    }                                                                 \
  } while (false)
#define CHECK_HR(expr)                                                      \
  do {                                                                      \
    if (FAILED(expr)) {                                                     \
      assert(0 && #expr);                                                   \
      throw std::runtime_error(__FILE__ "(" LINE_STRING "): FAILED( " #expr \
                                        " )");                              \
    }                                                                       \
  } while (false)

const uint32_t VENDOR_ID_AMD = 0x1002;
const uint32_t VENDOR_ID_NVIDIA = 0x10DE;
const uint32_t VENDOR_ID_INTEL = 0x8086;

template <typename T>
inline constexpr T AlignUp(T val, T align)
{
  return (val + align - 1) / align * align;
}

static const D3D12_RANGE EMPTY_RANGE = {0, 0};

static const wchar_t* VendorIDToStr(uint32_t vendorID)
{
  switch (vendorID) {
    case 0x10001:
      return L"VIV";
    case 0x10002:
      return L"VSI";
    case 0x10003:
      return L"KAZAN";
    case 0x10004:
      return L"CODEPLAY";
    case 0x10005:
      return L"MESA";
    case 0x10006:
      return L"POCL";
    case VENDOR_ID_AMD:
      return L"AMD";
    case VENDOR_ID_NVIDIA:
      return L"NVIDIA";
    case VENDOR_ID_INTEL:
      return L"Intel";
    case 0x1010:
      return L"ImgTec";
    case 0x13B5:
      return L"ARM";
    case 0x5143:
      return L"Qualcomm";
  }
  return L"";
}

static std::wstring SizeToStr(size_t size)
{
  if (size == 0) return L"0";

  wchar_t result[32];
  double size2 = (double)size;

  if (size2 >= 1024.0 * 1024.0 * 1024.0 * 1024.0) {
    swprintf_s(result, L"%.2f TB", size2 / (1024.0 * 1024.0 * 1024.0 * 1024.0));
  } else if (size2 >= 1024.0 * 1024.0 * 1024.0) {
    swprintf_s(result, L"%.2f GB", size2 / (1024.0 * 1024.0 * 1024.0));
  } else if (size2 >= 1024.0 * 1024.0) {
    swprintf_s(result, L"%.2f MB", size2 / (1024.0 * 1024.0));
  } else if (size2 >= 1024.0) {
    swprintf_s(result, L"%.2f KB", size2 / 1024.0);
  } else {
    swprintf_s(result, L"%llu B", size);
  }
  return result;
}

static void SetDefaultRasterizerDesc(D3D12_RASTERIZER_DESC& outDesc)
{
  outDesc.FillMode = D3D12_FILL_MODE_SOLID;
  outDesc.CullMode = D3D12_CULL_MODE_BACK;
  outDesc.FrontCounterClockwise = FALSE;
  outDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
  outDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
  outDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
  outDesc.DepthClipEnable = TRUE;
  outDesc.MultisampleEnable = FALSE;
  outDesc.AntialiasedLineEnable = FALSE;
  outDesc.ForcedSampleCount = 0;
  outDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
}

static void SetDefaultBlendDesc(D3D12_BLEND_DESC& outDesc)
{
  outDesc.AlphaToCoverageEnable = FALSE;
  outDesc.IndependentBlendEnable = FALSE;

  const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc = {
      FALSE,
      FALSE,
      D3D12_BLEND_ONE,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_OP_ADD,
      D3D12_BLEND_ONE,
      D3D12_BLEND_ZERO,
      D3D12_BLEND_OP_ADD,
      D3D12_LOGIC_OP_NOOP,
      D3D12_COLOR_WRITE_ENABLE_ALL};

  for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    outDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;
}

static void SetDefaultDepthStencilDesc(D3D12_DEPTH_STENCIL_DESC& outDesc)
{
  outDesc.DepthEnable = TRUE;
  outDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  outDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  outDesc.StencilEnable = FALSE;
  outDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
  outDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
  const D3D12_DEPTH_STENCILOP_DESC defaultStencilOp = {
      D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
      D3D12_COMPARISON_FUNC_ALWAYS};
  outDesc.FrontFace = defaultStencilOp;
  outDesc.BackFace = defaultStencilOp;
}

static std::wstring ConvertToWstring(std::string in)
{
  size_t newsize = std::strlen(in.c_str()) + 1;
  wchar_t* wcstring = new wchar_t[newsize];
  mbstowcs_s(nullptr, wcstring, newsize, in.c_str(), _TRUNCATE);

  std::wstring out(wcstring);
  delete[] wcstring;

  std::wcout << L"wide string:" << out << "\n";
  return out;
}

struct GPUSelection {
  UINT32 Index = UINT32_MAX;
  std::wstring Substring;
};

class DXGIUsage
{
public:
  void Init() { CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&m_DXGIFactory))); }

  IDXGIFactory4* GetDXGIFactory() const { return m_DXGIFactory; }

  void PrintAdapterList() const
  {
    UINT index = 0;
    IDXGIAdapter1* adapter;

    while (m_DXGIFactory->EnumAdapters1(index, &adapter) !=
           DXGI_ERROR_NOT_FOUND) {
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

      for (UINT i = 0;
           m_DXGIFactory->EnumAdapters1(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND;
           i++) {
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
