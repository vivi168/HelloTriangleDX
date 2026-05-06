#include "UtilsD3D12.h"

#include <iomanip>
#include <sstream>

#include <dxgidebug.h>

using Microsoft::WRL::ComPtr;

namespace IssouRHI
{
namespace D3D12
{
std::wstring StringToWstring(std::string_view s)
{
  int len = static_cast<int>(s.length());

  if (len == 0) return std::wstring();

  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), len, nullptr, 0);
  assert(size > 0);

  std::wstring ws(size, 0);
  int res = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), len, ws.data(), size);
  assert(res > 0);

  return ws;
}

void ReportLiveObjects()
{
#ifdef ENABLE_DEBUG_LAYER
  {
    Microsoft::WRL::ComPtr<IDXGIDebug1> dxgiDebug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug)))) {
      dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    }
  }
#endif
}

void PrintAdapterList()
{
  UINT index = 0;
  ComPtr<IDXGIFactory4> dxgiFactory;
  ComPtr<IDXGIAdapter1> adapter;

  CHECK_HR(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)));

  while (dxgiFactory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    const bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    const wchar_t* const suffix = isSoftware ? L" (SOFTWARE)" : L"";
    wprintf(L"Adapter %u: %s%s\n", index, desc.Description, suffix);

    adapter.Reset();
    ++index;
  }
}

void PrintStateObjectDesc(const D3D12_STATE_OBJECT_DESC* desc)
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
}  // namespace D3D12
}  // namespace IssouRHI
