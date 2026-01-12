#pragma once

const uint32_t VENDOR_ID_AMD = 0x1002;
const uint32_t VENDOR_ID_NVIDIA = 0x10DE;
const uint32_t VENDOR_ID_INTEL = 0x8086;

inline const wchar_t* VendorIDToStr(uint32_t vendorID)
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

inline std::wstring SizeToStr(size_t size)
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
