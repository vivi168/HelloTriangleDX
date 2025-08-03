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

inline void PrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
{
  std::wstringstream wstr;
  wstr << L"\n";
  wstr << L"--------------------------------------------------------------------\n";
  wstr << L"| D3D12 State Object 0x" << static_cast<const void*>(desc) << L": ";
  if (desc->Type == D3D12_STATE_OBJECT_TYPE_COLLECTION) wstr << L"Collection\n";
  if (desc->Type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) wstr << L"Raytracing Pipeline\n";

  auto ExportTree = [](UINT depth, UINT numExports, const D3D12_EXPORT_DESC* exports) {
    std::wostringstream woss;
    for (UINT i = 0; i < numExports; i++) {
      woss << L"|";
      if (depth > 0) {
        for (UINT j = 0; j < 2 * depth - 1; j++) woss << L" ";
      }
      woss << L" [" << i << L"]: ";
      if (exports[i].ExportToRename) woss << exports[i].ExportToRename << L" --> ";
      woss << exports[i].Name << L"\n";
    }
    return woss.str();
  };

  for (UINT i = 0; i < desc->NumSubobjects; i++) {
    wstr << L"| [" << i << L"]: ";
    switch (desc->pSubobjects[i].Type) {
      case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
        wstr << L"Global Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
        wstr << L"Local Root Signature 0x" << desc->pSubobjects[i].pDesc << L"\n";
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
        wstr << L"Node Mask: 0x" << std::hex << std::setfill(L'0') << std::setw(8)
             << *static_cast<const UINT*>(desc->pSubobjects[i].pDesc) << std::setw(0) << std::dec << L"\n";
        break;
      case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY: {
        wstr << L"DXIL Library 0x";
        auto lib = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(desc->pSubobjects[i].pDesc);
        wstr << lib->DXILLibrary.pShaderBytecode << L", " << lib->DXILLibrary.BytecodeLength << L" bytes\n";
        wstr << ExportTree(1, lib->NumExports, lib->pExports);
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION: {
        wstr << L"Existing Library 0x";
        auto collection = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(desc->pSubobjects[i].pDesc);
        wstr << collection->pExistingCollection << L"\n";
        wstr << ExportTree(1, collection->NumExports, collection->pExports);
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION: {
        wstr << L"Subobject to Exports Association (Subobject [";
        auto association = static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
        UINT index = static_cast<UINT>(association->pSubobjectToAssociate - desc->pSubobjects);
        wstr << index << L"])\n";
        for (UINT j = 0; j < association->NumExports; j++) {
          wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
        }
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION: {
        wstr << L"DXIL Subobjects to Exports Association (";
        auto association = static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(desc->pSubobjects[i].pDesc);
        wstr << association->SubobjectToAssociate << L")\n";
        for (UINT j = 0; j < association->NumExports; j++) {
          wstr << L"|  [" << j << L"]: " << association->pExports[j] << L"\n";
        }
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG: {
        wstr << L"Raytracing Shader Config\n";
        auto config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(desc->pSubobjects[i].pDesc);
        wstr << L"|  [0]: Max Payload Size: " << config->MaxPayloadSizeInBytes << L" bytes\n";
        wstr << L"|  [1]: Max Attribute Size: " << config->MaxAttributeSizeInBytes << L" bytes\n";
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG: {
        wstr << L"Raytracing Pipeline Config\n";
        auto config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(desc->pSubobjects[i].pDesc);
        wstr << L"|  [0]: Max Recursion Depth: " << config->MaxTraceRecursionDepth << L"\n";
        break;
      }
      case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP: {
        wstr << L"Hit Group (";
        auto hitGroup = static_cast<const D3D12_HIT_GROUP_DESC*>(desc->pSubobjects[i].pDesc);
        wstr << (hitGroup->HitGroupExport ? hitGroup->HitGroupExport : L"[none]") << L")\n";
        wstr << L"|  [0]: Any Hit Import: " << (hitGroup->AnyHitShaderImport ? hitGroup->AnyHitShaderImport : L"[none]")
             << L"\n";
        wstr << L"|  [1]: Closest Hit Import: "
             << (hitGroup->ClosestHitShaderImport ? hitGroup->ClosestHitShaderImport : L"[none]") << L"\n";
        wstr << L"|  [2]: Intersection Import: "
             << (hitGroup->IntersectionShaderImport ? hitGroup->IntersectionShaderImport : L"[none]") << L"\n";
        break;
      }
    }
    wstr << L"|--------------------------------------------------------------------\n";
  }
  wstr << L"\n";
  OutputDebugStringW(wstr.str().c_str());
}
