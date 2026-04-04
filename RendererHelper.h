#pragma once

inline std::filesystem::path GetExecutableDirectory()
{
#if !defined(WINAPI_FAMILY) || (WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
  wchar_t moduleName[_MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, moduleName, _MAX_PATH))
      throw std::system_error(std::error_code(static_cast<int>(GetLastError()), std::system_category()),
                              "GetModuleFileNameW");
  return std::filesystem::path(moduleName).parent_path();
#else
  // TODO: sorry
  return std::filesystem::current_path();
#endif
}

inline std::vector<uint8_t> ReadData(std::filesystem::path file)
{
  std::ifstream inFile(file, std::ios::in | std::ios::binary | std::ios::ate);

  if (!inFile && file.is_relative()) {
    inFile.open(GetExecutableDirectory() / file, std::ios::in | std::ios::binary | std::ios::ate);
  }

  if (!inFile) throw std::runtime_error("Read ShaderModule");

  const std::streampos len = inFile.tellg();
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  std::vector<uint8_t> v;
  v.resize(size_t(len));

  inFile.seekg(0, std::ios::beg);
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  inFile.read(reinterpret_cast<char*>(v.data()), len);
  if (!inFile) throw std::runtime_error("Read ShaderModule");

  inFile.close();

  return v;
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
