#pragma once

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

static const D3D12_RANGE EMPTY_RANGE = {0, 0};

const uint32_t VENDOR_ID_AMD = 0x1002;
const uint32_t VENDOR_ID_NVIDIA = 0x10DE;
const uint32_t VENDOR_ID_INTEL = 0x8086;

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
