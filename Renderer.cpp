#include "stdafx.h"

#include "Renderer.h"
#include "D3D12MemAlloc.h"
#include "Win32Application.h"

#include "d3dx12_root_signature.h"
#include "d3dx12_resource_helpers.h"
#include "d3dx12_barriers.h"

#include "Camera.h"
#include "Mesh.h"
#include "Terrain.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// ========== Data types

struct Geometry {
  ComPtr<ID3D12Resource> m_VertexBuffer;
  D3D12MA::Allocation* m_VertexBufferAllocation;
  D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;

  ComPtr<ID3D12Resource> m_IndexBuffer;
  D3D12MA::Allocation* m_IndexBufferAllocation;
  D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

  D3D12MA::Allocation* vBufferUploadHeapAllocation = nullptr;
  D3D12MA::Allocation* iBufferUploadHeapAllocation = nullptr;

  void Unload()
  {
    m_IndexBuffer.Reset();
    m_IndexBufferAllocation->Release();
    m_IndexBufferAllocation = nullptr;

    m_VertexBuffer.Reset();
    m_VertexBufferAllocation->Release();
    m_VertexBufferAllocation = nullptr;
  }
};

struct Texture {
  struct {
    uint16_t width;
    uint16_t height;
  } header;

  std::vector<uint8_t> pixels;
  std::string name;

  ComPtr<ID3D12Resource> resource;
  D3D12MA::Allocation* textureAllocation = nullptr;
  D3D12MA::Allocation* textureUploadAllocation = nullptr;

  D3D12_CPU_DESCRIPTOR_HANDLE srvCpuDescHandle;
  D3D12_GPU_DESCRIPTOR_HANDLE srvGpuDescHandle;

  void Read(std::string filename)
  {
    FILE* fp;
    fopen_s(&fp, filename.c_str(), "rb");
    assert(fp);

    name = filename;

    fread(&header, sizeof(header), 1, fp);
    pixels.resize(ImageSize());
    fread(pixels.data(), sizeof(uint8_t), ImageSize(), fp);
  }

  void Unload()
  {
    resource.Reset();
    textureAllocation->Release();
    textureAllocation = nullptr;
  }

  DXGI_FORMAT Format() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }

  uint32_t BytesPerPixel() const { return 4; }

  uint32_t Width() const { return header.width; }

  uint32_t Height() const { return header.height; }

  uint64_t BytesPerRow() const { return Width() * BytesPerPixel(); }

  uint64_t ImageSize() const { return Height() * BytesPerRow(); }
};

namespace Renderer
{
enum class PSO { Basic, Terrain, ColliderSurface };

struct Scene {
  struct SceneNode {
    Model3D* model;
    size_t cbIndex;
  };

  std::list<SceneNode> nodes;
  Terrain* terrain;
  Camera* camera;
} g_Scene;

struct FrameCB0_ALL {
  XMFLOAT4 Color;
  float time;
};

struct ObjectCB1_VS {
  XMFLOAT4X4 WorldViewProj;
  XMFLOAT4X4 WorldMatrix;
  XMFLOAT4X4 NormalMatrix;
};

struct DescriptorHeapListAllocator {
  void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
  {
    IM_ASSERT(m_Heap == nullptr && m_FreeIndices.empty());
    m_Heap = heap;
    D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
    m_HeapType = desc.Type;
    m_HeapStartCpu = m_Heap->GetCPUDescriptorHandleForHeapStart();
    m_HeapStartGpu = m_Heap->GetGPUDescriptorHandleForHeapStart();
    m_HeapHandleIncrement =
        device->GetDescriptorHandleIncrementSize(m_HeapType);
    m_FreeIndices.reserve((int)desc.NumDescriptors);
    for (int n = desc.NumDescriptors; n > 0; n--) m_FreeIndices.push_back(n);
  }
  void Destroy()
  {
    m_Heap = nullptr;
    m_FreeIndices.clear();
  }
  void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescHandle,
             D3D12_GPU_DESCRIPTOR_HANDLE* outGpuDescHandle)
  {
    assert(m_FreeIndices.size() > 0);
    int idx = m_FreeIndices.back();
    m_FreeIndices.pop_back();
    outCpuDescHandle->ptr = m_HeapStartCpu.ptr + (idx * m_HeapHandleIncrement);
    outGpuDescHandle->ptr = m_HeapStartGpu.ptr + (idx * m_HeapHandleIncrement);
  }
  void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle)
  {
    int cpu_idx =
        (int)((cpuDescHandle.ptr - m_HeapStartCpu.ptr) / m_HeapHandleIncrement);
    int gpu_idx =
        (int)((gpuDescHandle.ptr - m_HeapStartGpu.ptr) / m_HeapHandleIncrement);
    assert(cpu_idx == gpu_idx);
    m_FreeIndices.push_back(cpu_idx);
  }

private:
  ID3D12DescriptorHeap* m_Heap = nullptr;
  D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
  D3D12_CPU_DESCRIPTOR_HANDLE m_HeapStartCpu;
  D3D12_GPU_DESCRIPTOR_HANDLE m_HeapStartGpu;
  UINT m_HeapHandleIncrement;
  std::vector<int> m_FreeIndices;
};

// ========== Constants

#define ENABLE_DEBUG_LAYER true

static const bool ENABLE_CPU_ALLOCATION_CALLBACKS = true;
static const bool ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT = true;
static void* const CUSTOM_ALLOCATION_PRIVATE_DATA =
    (void*)(uintptr_t)0xDEADC0DE;

static const size_t OBJECT_CB_ALIGNED_SIZE =
    AlignUp<size_t>(sizeof(ObjectCB1_VS), 256);
static const size_t FRAME_BUFFER_COUNT = 3;

static const UINT PRESENT_SYNC_INTERVAL = 1;
static const UINT NUM_DESCRIPTORS_PER_HEAP = 1024;

static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
static const D3D_FEATURE_LEVEL FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1;

// ========== Static functions declarations

static void InitD3D();
static void InitFrameResources();
static void WaitForFrame(size_t frameIndex);
static void WaitGPUIdle(size_t frameIndex);
static std::wstring GetAssetFullPath(LPCWSTR assetName);
static void PrintAdapterInformation(IDXGIAdapter1* adapter);
static void LoadMesh3D(Mesh3D* mesh);
static Geometry* CreateGeometry(Mesh3D* mesh);
static Texture* CreateTexture(std::string name);

// ========== Global variables

static unsigned int g_CbNextIndex = 0;

static UINT g_Width;
static UINT g_Height;
static float g_AspectRatio;
static bool g_Raster = true;

static std::wstring g_Title;
static std::wstring g_AssetsPath;

// Pipeline objects
static DXGIUsage* g_DXGIUsage = nullptr;
static ComPtr<IDXGIAdapter1> g_Adapter;

static ComPtr<ID3D12Device> g_Device;
static DXGI_ADAPTER_DESC1 g_AdapterDesc;
static ComPtr<D3D12MA::Allocator> g_Allocator;
// Used only when ENABLE_CPU_ALLOCATION_CALLBACKS
static D3D12MA::ALLOCATION_CALLBACKS g_AllocationCallbacks;

// swapchain used to switch between render targets
static ComPtr<IDXGISwapChain3> g_SwapChain;
// container for command lists
static ComPtr<ID3D12CommandQueue> g_CommandQueue;
// we want enough allocators for each buffer * number of threads (we only have
// one thread)
static ComPtr<ID3D12CommandAllocator> g_CommandAllocators[FRAME_BUFFER_COUNT];
// a command list we can record commands into, then execute them to render the
// frame
static ComPtr<ID3D12GraphicsCommandList> g_CommandList;

// Synchronization objects.
// an object that is locked while our command list is being executed by the
// gpu. We need as many as we have allocators (more if we want to know when
// the gpu is finished with an asset)
static ComPtr<ID3D12Fence> g_Fences[FRAME_BUFFER_COUNT];
// a handle to an event when our g_Fences is unlocked by the gpu
static HANDLE g_FenceEvent;
// this value is incremented each frame. each g_Fences will have its own value
static UINT64 g_FenceValues[FRAME_BUFFER_COUNT];
static UINT g_FrameIndex;  // current rtv we are on

// Resources
// a descriptor heap to hold resources like the render targets
static ComPtr<ID3D12DescriptorHeap> g_RtvDescriptorHeap;
// number of render targets equal to buffer count
static ComPtr<ID3D12Resource> g_RenderTargets[FRAME_BUFFER_COUNT];
// size of the rtv descriptor on the g_Device (all front and back buffers will
// be the same size)
static UINT g_RtvDescriptorSize;

static ComPtr<ID3D12Resource> g_DepthStencilBuffer;
static D3D12MA::Allocation* g_DepthStencilAllocation;
static ComPtr<ID3D12DescriptorHeap> g_DepthStencilDescriptorHeap;

static ID3D12DescriptorHeap* g_SrvDescriptorHeap;
static DescriptorHeapListAllocator g_SrvDescHeapAlloc;

// PSO

static std::unordered_map<PSO, ComPtr<ID3D12PipelineState>>
    g_PipelineStateObjects;
static ComPtr<ID3D12RootSignature> g_RootSignature;

static D3D12MA::Allocation* g_ObjectCbUploadHeapAllocations[FRAME_BUFFER_COUNT];
static ComPtr<ID3D12Resource> g_ObjectCbUploadHeaps[FRAME_BUFFER_COUNT];
static void* g_ObjectCbAddress[FRAME_BUFFER_COUNT];

static ComPtr<ID3D12Resource> g_ConstantBufferUploadHeap[FRAME_BUFFER_COUNT];
static D3D12MA::Allocation*
    g_ConstantBufferUploadAllocation[FRAME_BUFFER_COUNT];
static void* g_ConstantBufferAddress[FRAME_BUFFER_COUNT];
static D3D12_CPU_DESCRIPTOR_HANDLE
    g_ConstantBufferSrvCpuDescHandle[FRAME_BUFFER_COUNT];
static D3D12_GPU_DESCRIPTOR_HANDLE
    g_ConstantBufferSrvGpuDescHandle[FRAME_BUFFER_COUNT];

static std::unordered_map<std::string, Geometry> g_Geometries;
static std::unordered_map<std::string, Texture> g_Textures;

static std::atomic<size_t> g_CpuAllocationCount{0};

// ========== Public functions

void InitWindow(UINT width, UINT height, std::wstring name)
{
  g_Width = width;
  g_Height = height;
  g_AspectRatio = static_cast<float>(width) / static_cast<float>(height);
}

void InitAdapter(DXGIUsage* dxgiUsage, IDXGIAdapter1* adapter)
{
  g_DXGIUsage = dxgiUsage;
  assert(g_DXGIUsage);

  g_Adapter = adapter;
  assert(g_Adapter);

  CHECK_HR(g_Adapter->GetDesc1(&g_AdapterDesc));
}

void Init()
{
  InitD3D();
  InitFrameResources();
}

void LoadAssets()
{
  CHECK_HR(g_CommandList->Reset(g_CommandAllocators[g_FrameIndex].Get(), NULL));

  for (auto node : g_Scene.nodes) {
    LoadMesh3D(node.model->mesh);
  }

  // End of initial command list
  {
    g_CommandList->Close();

    // Now we execute the command list to upload the initial assets
    // (triangle data)
    ID3D12CommandList* ppCommandLists[] = {g_CommandList.Get()};
    g_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists),
                                        ppCommandLists);

    // increment the fence value now, otherwise the buffer might not be uploaded
    // by the time we start drawing
    WaitGPUIdle(g_FrameIndex);

    // TODO: need method + ensure not null
    for (auto& [k, tex] : g_Textures) {
      tex.textureUploadAllocation->Release();
    }

    // TODO: need method + ensure not null
    for (auto& [k, geom] : g_Geometries) {
      geom.vBufferUploadHeapAllocation->Release();
      geom.iBufferUploadHeapAllocation->Release();
    }
  }
}

void Update(float time)
{
  // Per frame constant buffer
  {
    const float r = sin(0.5f * time * (XM_PI * 2.f)) * 0.5f + 0.5f;
    FrameCB0_ALL cb;
    cb.Color = XMFLOAT4(r, 1.f, 1.f, 1.f);
    cb.time = time;
    memcpy(g_ConstantBufferAddress[g_FrameIndex], &cb, sizeof(cb));
  }

  // Per object constant buffer
  {
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(
        45.f * (XM_PI / 180.f), g_AspectRatio, 0.1f, 1000.f);

    XMMATRIX view = g_Scene.camera->LookAt();
    XMMATRIX viewProjection = view * projection;

    for (auto& node : g_Scene.nodes) {
      ObjectCB1_VS cb;

      XMMATRIX worldViewProjection = node.model->WorldMatrix() * viewProjection;
      XMStoreFloat4x4(&cb.WorldViewProj, worldViewProjection);
      XMStoreFloat4x4(&cb.WorldMatrix, node.model->WorldMatrix());

      XMMATRIX normalMatrix = XMMatrixTranspose(
          XMMatrixInverse(nullptr, node.model->WorldMatrix()));
      XMStoreFloat4x4(&cb.NormalMatrix, normalMatrix);

      memcpy((uint8_t*)g_ObjectCbAddress[g_FrameIndex] + node.cbIndex, &cb,
             sizeof(cb));
    }
  }

  // ImGui
  {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
  }

  {
    ImGui::Begin("Ray tracing");
    ImGui::Checkbox("Raster", &g_Raster);
    ImGui::End();
  }
}

// TODO: render ColliderSurfaces
// DrawIndexed
// Need to add a new PSO
// Add a PSO helper struct ?
void Render()
{
  // swap the current rtv buffer index so we draw on the correct buffer
  g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
  // We have to wait for the gpu to finish with the command allocator before we
  // reset it
  WaitForFrame(g_FrameIndex);
  // increment g_FenceValues for next frame
  g_FenceValues[g_FrameIndex]++;

  // we can only reset an allocator once the gpu is done with it. Resetting an
  // allocator frees the memory that the command list was stored in
  CHECK_HR(g_CommandAllocators[g_FrameIndex]->Reset());

  // reset the command list. by resetting the command list we are putting it
  // into a recording state so we can start recording commands into the command
  // allocator. The command allocator that we reference here may have multiple
  // command lists associated with it, but only one can be recording at any
  // time. Make sure that any other command lists associated to this command
  // allocator are in the closed state (not recording). Here you will pass an
  // initial pipeline state object as the second parameter, but in this tutorial
  // we are only clearing the rtv, and do not actually need anything but an
  // initial default pipeline, which is what we get by setting the second
  // parameter to NULL
  CHECK_HR(g_CommandList->Reset(g_CommandAllocators[g_FrameIndex].Get(), NULL));

  // here we start recording commands into the g_CommandList (which all the
  // commands will be stored in the g_CommandAllocators)

  // transition the "g_FrameIndex" render target from the present state to the
  // render target state so the command list draws to it starting from here
  auto presentToRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
      g_RenderTargets[g_FrameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
      D3D12_RESOURCE_STATE_RENDER_TARGET);
  g_CommandList->ResourceBarrier(1, &presentToRenderTargetBarrier);

  // here we again get the handle to our current render target view so we can
  // set it as the render target in the output merger stage of the pipeline
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {
      g_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr +
      g_FrameIndex * g_RtvDescriptorSize};
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
      g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

  // set the render target for the output merger stage (the output of the
  // pipeline)
  g_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

  g_CommandList->ClearDepthStencilView(
      g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
      D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  // Clear the render target by using the ClearRenderTargetView command
  if (g_Raster) {
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  } else {
    const float clearColor[] = {0.6f, 0.8f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  }

  g_CommandList->SetGraphicsRootSignature(g_RootSignature.Get());

  ID3D12DescriptorHeap* descriptorHeaps[] = {g_SrvDescriptorHeap};
  g_CommandList->SetDescriptorHeaps(1, descriptorHeaps);

  g_CommandList->SetGraphicsRootDescriptorTable(
      0, g_ConstantBufferSrvGpuDescHandle[g_FrameIndex]);

  D3D12_VIEWPORT viewport{0.f, 0.f, (float)g_Width, (float)g_Height, 0.f, 1.f};
  g_CommandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissorRect{0, 0, g_Width, g_Height};
  g_CommandList->RSSetScissorRects(1, &scissorRect);

  g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::Basic].Get());

  for (const auto node : g_Scene.nodes) {
    auto mesh = node.model->mesh;
    auto geom = mesh->geometry;
    g_CommandList->IASetVertexBuffers(0, 1, &geom->m_VertexBufferView);
    g_CommandList->IASetIndexBuffer(&geom->m_IndexBufferView);
    g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    g_CommandList->SetGraphicsRootConstantBufferView(
        1, g_ObjectCbUploadHeaps[g_FrameIndex]->GetGPUVirtualAddress() +
               node.cbIndex);

    for (const auto& subset : mesh->subsets) {
      g_CommandList->SetGraphicsRootDescriptorTable(
          2, subset.texture->srvGpuDescHandle);
      g_CommandList->DrawIndexedInstanced(subset.count, 1, subset.start, 0, 0);
    }
  }

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_CommandList.Get());

  // transition the "g_FrameIndex" render target from the render target state to
  // the present state. If the debug layer is enabled, you will receive a
  // warning if present is called on the render target when it's not in the
  // present state
  auto renderTargetToPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
      g_RenderTargets[g_FrameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_PRESENT);
  g_CommandList->ResourceBarrier(1, &renderTargetToPresentBarrier);

  CHECK_HR(g_CommandList->Close());

  // ==========

  // create an array of command lists (only one command list here)
  ID3D12CommandList* ppCommandLists[] = {g_CommandList.Get()};

  // execute the array of command lists
  g_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // this command goes in at the end of our command queue. we will know when our
  // command queue has finished because the g_Fences value will be set to
  // "g_FenceValues" from the GPU since the command queue is being executed on
  // the GPU
  CHECK_HR(g_CommandQueue->Signal(g_Fences[g_FrameIndex].Get(),
                                  g_FenceValues[g_FrameIndex]));

  // present the current backbuffer
  CHECK_HR(g_SwapChain->Present(PRESENT_SYNC_INTERVAL, 0));
}

void Cleanup()
{
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  // wait for the gpu to finish all frames
  for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
    WaitForFrame(i);
    CHECK_HR(g_CommandQueue->Wait(g_Fences[i].Get(), g_FenceValues[i]));
  }

  // get swapchain out of full screen before exiting
  BOOL fs = false;
  CHECK_HR(g_SwapChain->GetFullscreenState(&fs, NULL));

  if (fs) g_SwapChain->SetFullscreenState(false, NULL);

  WaitGPUIdle(0);

  // TODO: need method + ensure not null
  for (auto& [k, tex] : g_Textures) {
    // TODO: do this from Unload()
    g_SrvDescHeapAlloc.Free(tex.srvCpuDescHandle, tex.srvGpuDescHandle);
    tex.Unload();
  }

  // TODO: need method + ensure not null
  for (auto& [k, geom] : g_Geometries) {
    geom.Unload();
  }

  g_PipelineStateObjects[PSO::Basic].Reset();
  g_PipelineStateObjects[PSO::Terrain].Reset();
  g_RootSignature.Reset();

  CloseHandle(g_FenceEvent);
  g_CommandList.Reset();
  g_CommandQueue.Reset();

  for (size_t i = FRAME_BUFFER_COUNT; i--;) {
    g_ObjectCbUploadHeaps[i].Reset();
    g_ObjectCbUploadHeapAllocations[i]->Release();
    g_ObjectCbUploadHeapAllocations[i] = nullptr;
    g_ConstantBufferUploadHeap[i].Reset();
    g_ConstantBufferUploadAllocation[i]->Release();
    g_ConstantBufferUploadAllocation[i] = nullptr;
  }

  g_SrvDescriptorHeap->Release();
  g_SrvDescriptorHeap = nullptr;
  g_DepthStencilDescriptorHeap.Reset();
  g_DepthStencilBuffer.Reset();
  g_DepthStencilAllocation->Release();
  g_DepthStencilAllocation = nullptr;
  g_RtvDescriptorHeap.Reset();

  for (size_t i = FRAME_BUFFER_COUNT; i--;) {
    g_RenderTargets[i].Reset();
    g_CommandAllocators[i].Reset();
    g_Fences[i].Reset();
  }

  g_Allocator.Reset();

  if (ENABLE_CPU_ALLOCATION_CALLBACKS) {
    assert(g_CpuAllocationCount.load() == 0);
  }

  g_Device.Reset();
  g_SwapChain.Reset();
}

void PrintStatsString()
{
  WCHAR* statsString = NULL;
  g_Allocator->BuildStatsString(&statsString, TRUE);
  wprintf(L"%s\n", statsString);
  g_Allocator->FreeStatsString(statsString);
}

UINT GetWidth() { return g_Width; }

UINT GetHeight() { return g_Height; }

const WCHAR* GetTitle() { return g_Title.c_str(); }

void SetSceneCamera(Camera* cam) { g_Scene.camera = cam; }

void AppendToScene(Model3D* model)
{
  size_t cbIndex = OBJECT_CB_ALIGNED_SIZE * g_CbNextIndex++;

  g_Scene.nodes.push_back({model, cbIndex});
}

// ========== Static functions

static void* CustomAllocate(size_t Size, size_t Alignment, void* pPrivateData)
{
  assert(pPrivateData == CUSTOM_ALLOCATION_PRIVATE_DATA);

  void* memory = _aligned_malloc(Size, Alignment);

  if (ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT) {
    wprintf(L"Allocate Size=%llu Alignment=%llu -> %p\n", Size, Alignment,
            memory);
  }

  g_CpuAllocationCount++;

  return memory;
}

static void CustomFree(void* pMemory, void* pPrivateData)
{
  assert(pPrivateData == CUSTOM_ALLOCATION_PRIVATE_DATA);

  if (pMemory) {
    g_CpuAllocationCount--;

    if (ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT) {
      wprintf(L"Free %p\n", pMemory);
    }

    _aligned_free(pMemory);
  }
}

static void InitD3D()
{
#ifdef ENABLE_DEBUG_LAYER
  ID3D12Debug* debug;

  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    debug->EnableDebugLayer();
#endif

  // Create Device
  {
    ID3D12Device* device = nullptr;
    CHECK_HR(D3D12CreateDevice(g_Adapter.Get(), FEATURE_LEVEL,
                               IID_PPV_ARGS(&device)));
    g_Device.Attach(device);

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    CHECK_HR(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                           &options5, sizeof(options5)));
    assert(options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);
  }

#ifdef ENABLE_DEBUG_LAYER
  ID3D12InfoQueue* pInfoQueue = nullptr;
  g_Device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
  pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
  pInfoQueue->Release();
  debug->Release();
#endif

  // Create Memory allocator
  {
    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
    desc.pDevice = g_Device.Get();
    desc.pAdapter = g_Adapter.Get();

    if (ENABLE_CPU_ALLOCATION_CALLBACKS) {
      g_AllocationCallbacks.pAllocate = &CustomAllocate;
      g_AllocationCallbacks.pFree = &CustomFree;
      g_AllocationCallbacks.pPrivateData = CUSTOM_ALLOCATION_PRIVATE_DATA;
      desc.pAllocationCallbacks = &g_AllocationCallbacks;
    }

    CHECK_HR(D3D12MA::CreateAllocator(&desc, &g_Allocator));

    PrintAdapterInformation(g_Adapter.Get());
  }

  // Create Command Queue
  {
    D3D12_COMMAND_QUEUE_DESC cqDesc = {};  // use default values

    ID3D12CommandQueue* commandQueue = nullptr;
    CHECK_HR(
        g_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)));
    g_CommandQueue.Attach(commandQueue);
  }

  // Create Command Allocator
  {
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      ID3D12CommandAllocator* commandAllocator = nullptr;
      CHECK_HR(g_Device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
      g_CommandAllocators[i].Attach(commandAllocator);
    }

    // create the command list with the first allocator
    CHECK_HR(g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         g_CommandAllocators[0].Get(), NULL,
                                         IID_PPV_ARGS(&g_CommandList)));

    // command lists are created in the recording state. our main loop will set
    // it up for recording again so close it now
    g_CommandList->Close();
  }

  // Create Synchronization objects
  {
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      ID3D12Fence* fence = nullptr;
      CHECK_HR(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                     IID_PPV_ARGS(&fence)));
      g_Fences[i].Attach(fence);
      g_FenceValues[i] = 0;
    }

    g_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(g_FenceEvent);
  }

  // Create Swapchain
  {
    // this is to describe our display mode
    DXGI_MODE_DESC backBufferDesc = {};
    backBufferDesc.Width = g_Width;
    backBufferDesc.Height = g_Height;
    backBufferDesc.Format = RENDER_TARGET_FORMAT;

    // Describe and create the swap chain.
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = FRAME_BUFFER_COUNT;
    swapChainDesc.BufferDesc = backBufferDesc;
    // this says the pipeline will render to this swap chain
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // dxgi will discard the buffer (data) after we call present
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.OutputWindow = Win32Application::GetHwnd();

    swapChainDesc.SampleDesc.Count = 1;  // our multi-sampling description
    swapChainDesc.SampleDesc.Quality = 0;

    swapChainDesc.Windowed = true;  // set to true, then if in fullscreen must
                                    // call SetFullScreenState with true for
                                    // full screen to get uncapped fps

    IDXGISwapChain* tempSwapChain;

    // The queue will be flushed once the swap chain is created. Give it the
    // swap chain description we created above and store the created swap chain
    // in a temp IDXGISwapChain interface
    CHECK_HR(g_DXGIUsage->GetDXGIFactory()->CreateSwapChain(
        g_CommandQueue.Get(), &swapChainDesc, &tempSwapChain));

    g_SwapChain.Attach(static_cast<IDXGISwapChain3*>(tempSwapChain));

    g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
  }
}

static void InitFrameResources()
{
  // assets path
  WCHAR assetsPath[512];
  GetAssetsPath(assetsPath, _countof(assetsPath));
  g_AssetsPath = assetsPath;

  // RTV descriptor heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FRAME_BUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    // This heap will not be directly referenced by the shaders (not shader
    // visible), as this will store the output from the pipeline otherwise we
    // would set the heap's flag to D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
    CHECK_HR(g_Device->CreateDescriptorHeap(&rtvHeapDesc,
                                            IID_PPV_ARGS(&rtvDescriptorHeap)));
    g_RtvDescriptorHeap.Attach(rtvDescriptorHeap);

    // get the size of a descriptor in this heap (this is a rtv heap, so only
    // rtv descriptors should be stored in it. Descriptor sizes may vary from
    // g_Device to g_Device, which is why there is no set size and we must ask
    // the g_Device to give us the size. we will use this size to increment a
    // descriptor handle offset
    g_RtvDescriptorSize = g_Device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // get a handle to the first descriptor in the descriptor heap. a handle is
    // basically a pointer, but we cannot literally use it like a c++ pointer.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{
        g_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart()};

    // Create a RTV for each buffer (double buffering is two buffers, tripple
    // buffering is 3).
    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      // first we get the n'th buffer in the swap chain and store it in the n'th
      // position of our ID3D12Resource array
      ID3D12Resource* res = nullptr;
      CHECK_HR(g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&res)));
      g_RenderTargets[i].Attach(res);

      // the we "create" a render target view which binds the swap chain buffer
      // (ID3D12Resource[n]) to the rtv handle
      g_Device->CreateRenderTargetView(g_RenderTargets[i].Get(), nullptr,
                                       rtvHandle);

      // we increment the rtv handle by the rtv descriptor size we got above
      rtvHandle.ptr += g_RtvDescriptorSize;
    }
  }

  // DSV descriptor heap
  {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    CHECK_HR(g_Device->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(&g_DepthStencilDescriptorHeap)));

    D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
    depthOptimizedClearValue.Format = DEPTH_STENCIL_FORMAT;
    depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
    depthOptimizedClearValue.DepthStencil.Stencil = 0;

    D3D12MA::ALLOCATION_DESC depthStencilAllocDesc = {};
    depthStencilAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC depthStencilResourceDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(DEPTH_STENCIL_FORMAT, g_Width, g_Height);
    depthStencilResourceDesc.MipLevels = 1;
    depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    CHECK_HR(g_Allocator->CreateResource(
        &depthStencilAllocDesc, &depthStencilResourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue,
        &g_DepthStencilAllocation, IID_PPV_ARGS(&g_DepthStencilBuffer)));
    CHECK_HR(g_DepthStencilBuffer->SetName(L"Depth/Stencil Resource Heap"));
    g_DepthStencilAllocation->SetName(L"Depth/Stencil Resource Heap");

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
    depthStencilDesc.Format = DEPTH_STENCIL_FORMAT;
    depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
    g_Device->CreateDepthStencilView(
        g_DepthStencilBuffer.Get(), &depthStencilDesc,
        g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
  }

  // CBV_SRV_UAV descriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = NUM_DESCRIPTORS_PER_HEAP;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  CHECK_HR(g_Device->CreateDescriptorHeap(&heapDesc,
                                          IID_PPV_ARGS(&g_SrvDescriptorHeap)));

  g_SrvDescHeapAlloc.Create(g_Device.Get(), g_SrvDescriptorHeap);

  // Setup Platform/Renderer backends
  ImGui_ImplDX12_InitInfo initInfo = {};
  initInfo.Device = g_Device.Get();
  initInfo.CommandQueue = g_CommandQueue.Get();
  initInfo.NumFramesInFlight = FRAME_BUFFER_COUNT;
  initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
  initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
  // Allocating SRV descriptors (for textures) is up to the application, so we
  // provide callbacks. (current version of the backend will only allocate one
  // descriptor, future versions will need to allocate more)
  initInfo.SrvDescriptorHeap = g_SrvDescriptorHeap;
  initInfo.SrvDescriptorAllocFn =
      [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle,
         D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle) {
        return g_SrvDescHeapAlloc.Alloc(outCpuHandle, outGpuHandle);
      };
  initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*,
                                    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
    return g_SrvDescHeapAlloc.Free(cpuHandle, gpuHandle);
  };
  ImGui_ImplDX12_Init(&initInfo);

  // ImGui_ImplDX12_Init(g_Device.Get(), FRAME_BUFFER_COUNT,
  //                     DXGI_FORMAT_R8G8B8A8_UNORM, g_MainDescriptorHeap);

  // Root Signature
  {
    // Descriptor ranges
    CD3DX12_DESCRIPTOR_RANGE descriptorRanges[2] = {};
    descriptorRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);  // b0
    descriptorRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);  // t0

    // Root parameters
    // Applications should sort entries in the root signature from most
    // frequently changing to least.
    CD3DX12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].InitAsDescriptorTable(1, &descriptorRanges[0]);
    rootParameters[1].InitAsConstantBufferView(1);  // b1
    rootParameters[2].InitAsDescriptorTable(1, &descriptorRanges[1]);

    // Static sampler
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);

    // Root Signature
    D3D12_ROOT_SIGNATURE_FLAGS flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(
        3, rootParameters, 1, staticSamplers, flags);

    ID3DBlob* signatureBlobPtr;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                  &signatureBlobPtr, nullptr));

    ID3D12RootSignature* rootSignature = nullptr;
    CHECK_HR(g_Device->CreateRootSignature(
        0, signatureBlobPtr->GetBufferPointer(),
        signatureBlobPtr->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    g_RootSignature.Attach(rootSignature);
  }

  // Constant Buffers
  // per frame CB
  {
    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    for (int i = 0; i < FRAME_BUFFER_COUNT; i++) {
      CHECK_HR(g_Allocator->CreateResource(
          &allocDesc, &CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          &g_ConstantBufferUploadAllocation[i],
          IID_PPV_ARGS(&g_ConstantBufferUploadHeap[i])));
      g_ConstantBufferUploadHeap[i]->SetName(
          L"Constant Buffer (per frame) Upload Resource Heap");
      g_ConstantBufferUploadAllocation[i]->SetName(
          L"Constant Buffer (per frame) Upload Resource Heap");

      D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
      cbvDesc.BufferLocation =
          g_ConstantBufferUploadHeap[i]->GetGPUVirtualAddress();
      cbvDesc.SizeInBytes = AlignUp<UINT>(sizeof(FrameCB0_ALL), 256);

      // TODO: Alloc Here?
      g_SrvDescHeapAlloc.Alloc(&g_ConstantBufferSrvCpuDescHandle[i],
                               &g_ConstantBufferSrvGpuDescHandle[i]);
      g_Device->CreateConstantBufferView(&cbvDesc,
                                         g_ConstantBufferSrvCpuDescHandle[i]);

      CHECK_HR(g_ConstantBufferUploadHeap[i]->Map(0, &EMPTY_RANGE,
                                                  &g_ConstantBufferAddress[i]));
    }
  }

  // per object CB
  {
    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      CHECK_HR(g_Allocator->CreateResource(
          &allocDesc,
          // size of the resource heap. Must be a multiple of 64KB for
          // single-textures and constant buffers
          &CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
          // will be data that is read from so we keep it in the generic read
          // state
          D3D12_RESOURCE_STATE_GENERIC_READ,
          nullptr,  // we do not have use an optimized clear value for constant
                    // buffers
          &g_ObjectCbUploadHeapAllocations[i],
          IID_PPV_ARGS(&g_ObjectCbUploadHeaps[i])));

      std::wstring name = L"Constant Buffer (per obj) Upload Resource Heap - " +
                          std::to_wstring(i);
      g_ObjectCbUploadHeaps[i]->SetName(name.c_str());
      g_ObjectCbUploadHeapAllocations[i]->SetName(name.c_str());

      CHECK_HR(g_ObjectCbUploadHeaps[i]->Map(0, &EMPTY_RANGE,
                                             &g_ObjectCbAddress[i]));
    }
  }

  // Pipeline State for static objects
  {
    // Vertex Shader
    auto vertexShaderBlob = ReadData(GetAssetFullPath(L"VS.cso").c_str());
    D3D12_SHADER_BYTECODE vertexShader = {vertexShaderBlob.data(),
                                          vertexShaderBlob.size()};

    // Pixel Shader
    auto pixelShaderBlob = ReadData(GetAssetFullPath(L"PS.cso").c_str());
    D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob.data(),
                                         pixelShaderBlob.size()};

    // create input layout
    // The input layout is used by the Input Assembler so that it knows how to
    // read the vertex data bound to it.
    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // create a pipeline state object (PSO)
    // In a real application, you will have many pso's. for each different
    // shader or different combinations of shaders, different blend states or
    // different rasterizer states, different topology types (point, line,
    // triangle, patch), or a different number of render targets you will need a
    // pso VS is the only required shader for a pso. You might be wondering when
    // a case would be where you only set the VS. It's possible that you have a
    // pso that only outputs data with the stream output, and not on a render
    // target, which means you would not need anything after the stream output.
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout.NumElements = _countof(inputLayout);
    psoDesc.InputLayout.pInputElementDescs = inputLayout;
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.VS = vertexShader;
    psoDesc.PS = pixelShader;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT;
    psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
    psoDesc.SampleDesc.Count = 1;  // must be the same sample description as the
                                   // swapchain and depth/stencil buffer
    psoDesc.SampleDesc.Quality = 0;
    // sample mask has to do with multi-sampling. 0xffffffff means point
    // sampling is done
    psoDesc.SampleMask = 0xffffffff;
    SetDefaultRasterizerDesc(psoDesc.RasterizerState);
    SetDefaultBlendDesc(psoDesc.BlendState);
    psoDesc.NumRenderTargets = 1;  // we are only binding one render target
    SetDefaultDepthStencilDesc(psoDesc.DepthStencilState);

    // create the pso
    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::Basic].Attach(pipelineStateObject);
  }

  // Pipeline state for terrain
  {
    // Vertex Shader
    auto vertexShaderBlob =
        ReadData(GetAssetFullPath(L"TerrainVS.cso").c_str());
    D3D12_SHADER_BYTECODE vertexShader = {vertexShaderBlob.data(),
                                          vertexShaderBlob.size()};

    // Pixel Shader
    auto pixelShaderBlob = ReadData(GetAssetFullPath(L"TerrainPS.cso").c_str());
    D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob.data(),
                                         pixelShaderBlob.size()};

    // create input layout
    // The input layout is used by the Input Assembler so that it knows how to
    // read the vertex data bound to it.
    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout.NumElements = _countof(inputLayout);
    psoDesc.InputLayout.pInputElementDescs = inputLayout;
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.VS = vertexShader;
    psoDesc.PS = pixelShader;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT;
    psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
    psoDesc.SampleDesc.Count = 1;  // must be the same sample description as the
                                   // swapchain and depth/stencil buffer
    psoDesc.SampleDesc.Quality = 0;
    // sample mask has to do with multi-sampling. 0xffffffff means point
    // sampling is done
    psoDesc.SampleMask = 0xffffffff;
    SetDefaultRasterizerDesc(psoDesc.RasterizerState);
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    SetDefaultBlendDesc(psoDesc.BlendState);
    psoDesc.NumRenderTargets = 1;  // we are only binding one render target
    SetDefaultDepthStencilDesc(psoDesc.DepthStencilState);

    // create the pso
    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::Terrain].Attach(pipelineStateObject);
  }
}

// wait until gpu is finished with command list
static void WaitForFrame(size_t frameIndex)
{
  // if the current g_Fences value is still less than "g_FenceValues", then we
  // know the GPU has not finished executing the command queue since it has
  // not reached the "g_CommandQueue->Signal(g_Fences, g_FenceValues)" command
  if (g_Fences[frameIndex]->GetCompletedValue() < g_FenceValues[frameIndex]) {
    // we have the g_Fences create an event which is signaled once the
    // g_Fences's current value is "g_FenceValues"
    CHECK_HR(g_Fences[frameIndex]->SetEventOnCompletion(
        g_FenceValues[frameIndex], g_FenceEvent));

    // We will wait until the g_Fences has triggered the event that it's
    // current value has reached "g_FenceValues". once it's value has reached
    // "g_FenceValues", we know the command queue has finished executing
    WaitForSingleObject(g_FenceEvent, INFINITE);
  }
}

static void WaitGPUIdle(size_t frameIndex)
{
  g_FenceValues[frameIndex]++;
  CHECK_HR(g_CommandQueue->Signal(g_Fences[frameIndex].Get(),
                                  g_FenceValues[frameIndex]));
  WaitForFrame(frameIndex);
}

// Helper function for resolving the full path of assets.
static std::wstring GetAssetFullPath(LPCWSTR assetName)
{
  return g_AssetsPath + assetName;
}

static void PrintAdapterInformation(IDXGIAdapter1* adapter)
{
  wprintf(L"DXGI_ADAPTER_DESC1:\n");
  wprintf(L"    Description = %s\n", g_AdapterDesc.Description);
  wprintf(L"    VendorId = 0x%X (%s)\n", g_AdapterDesc.VendorId,
          VendorIDToStr(g_AdapterDesc.VendorId));
  wprintf(L"    DeviceId = 0x%X\n", g_AdapterDesc.DeviceId);
  wprintf(L"    SubSysId = 0x%X\n", g_AdapterDesc.SubSysId);
  wprintf(L"    Revision = 0x%X\n", g_AdapterDesc.Revision);
  wprintf(L"    DedicatedVideoMemory = %zu B (%s)\n",
          g_AdapterDesc.DedicatedVideoMemory,
          SizeToStr(g_AdapterDesc.DedicatedVideoMemory).c_str());
  wprintf(L"    DedicatedSystemMemory = %zu B (%s)\n",
          g_AdapterDesc.DedicatedSystemMemory,
          SizeToStr(g_AdapterDesc.DedicatedSystemMemory).c_str());
  wprintf(L"    SharedSystemMemory = %zu B (%s)\n",
          g_AdapterDesc.SharedSystemMemory,
          SizeToStr(g_AdapterDesc.SharedSystemMemory).c_str());

  const D3D12_FEATURE_DATA_D3D12_OPTIONS& options =
      g_Allocator->GetD3D12Options();
  wprintf(L"D3D12_FEATURE_DATA_D3D12_OPTIONS:\n");
  wprintf(L"    StandardSwizzle64KBSupported = %u\n",
          options.StandardSwizzle64KBSupported ? 1 : 0);
  wprintf(L"    CrossAdapterRowMajorTextureSupported = %u\n",
          options.CrossAdapterRowMajorTextureSupported ? 1 : 0);

  switch (options.ResourceHeapTier) {
    case D3D12_RESOURCE_HEAP_TIER_1:
      wprintf(L"    ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1\n");
      break;
    case D3D12_RESOURCE_HEAP_TIER_2:
      wprintf(L"    ResourceHeapTier = D3D12_RESOURCE_HEAP_TIER_2\n");
      break;
    default:
      assert(0);
  }

  ComPtr<IDXGIAdapter3> adapter3;

  if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)))) {
    wprintf(L"DXGI_QUERY_VIDEO_MEMORY_INFO:\n");

    for (UINT groupIndex = 0; groupIndex < 2; ++groupIndex) {
      const DXGI_MEMORY_SEGMENT_GROUP group =
          groupIndex == 0 ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL
                          : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
      const wchar_t* const groupName =
          groupIndex == 0 ? L"DXGI_MEMORY_SEGMENT_GROUP_LOCAL"
                          : L"DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL";
      DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
      CHECK_HR(adapter3->QueryVideoMemoryInfo(0, group, &info));

      wprintf(L"    %s:\n", groupName);
      wprintf(L"        Budget = %llu B (%s)\n", info.Budget,
              SizeToStr(info.Budget).c_str());
      wprintf(L"        CurrentUsage = %llu B (%s)\n", info.CurrentUsage,
              SizeToStr(info.CurrentUsage).c_str());
      wprintf(L"        AvailableForReservation = %llu B (%s)\n",
              info.AvailableForReservation,
              SizeToStr(info.AvailableForReservation).c_str());
      wprintf(L"        CurrentReservation = %llu B (%s)\n",
              info.CurrentReservation,
              SizeToStr(info.CurrentReservation).c_str());
    }
  }

  assert(g_Device);
  D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1 = {};

  if (SUCCEEDED(g_Device->CheckFeatureSupport(
          D3D12_FEATURE_ARCHITECTURE1, &architecture1, sizeof architecture1))) {
    wprintf(L"D3D12_FEATURE_DATA_ARCHITECTURE1:\n");
    wprintf(L"    UMA: %u\n", architecture1.UMA ? 1 : 0);
    wprintf(L"    CacheCoherentUMA: %u\n",
            architecture1.CacheCoherentUMA ? 1 : 0);
    wprintf(L"    IsolatedMMU: %u\n", architecture1.IsolatedMMU ? 1 : 0);
  }
}

static void LoadMesh3D(Mesh3D* mesh)
{
  auto geomNeedle = g_Geometries.find(mesh->name);
  if (geomNeedle == std::end(g_Geometries)) {
    printf("CREATE GEOMETRY %s\n", mesh->name.c_str());
    mesh->geometry = CreateGeometry(mesh);
  } else {
    printf("GEOMETRY ALREADY EXISTS %s\n", mesh->name.c_str());
    mesh->geometry = &geomNeedle->second;
  }

  for (auto& subset : mesh->subsets) {
    // TODO: get assets path
    std::string name(subset.name);
    std::string fullName = "assets/" + name;

    auto needle = g_Textures.find(fullName);
    if (needle == std::end(g_Textures)) {
      subset.texture = CreateTexture(fullName);
      printf("CREATE TEXTURE %s\n", fullName.c_str());
    } else {
      subset.texture = &needle->second;
      printf("TEXTURE ALREADY EXISTS %s\n", fullName.c_str());
    }
  }
}

static Geometry* CreateGeometry(Mesh3D* mesh)
{
  Geometry geom;

  // Vertex Buffer
  {
    const UINT64 vBufferSize = mesh->VertexBufferSize();

    // create default heap
    // Default heap is memory on the GPU. Only the GPU has access to this
    // memory. To get data into this heap, we will have to upload the data using
    // an upload heap
    D3D12MA::ALLOCATION_DESC vertexBufferAllocDesc = {};
    vertexBufferAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    ID3D12Resource* vertexBufferPtr;
    CHECK_HR(g_Allocator->CreateResource(
        &vertexBufferAllocDesc, &CD3DX12_RESOURCE_DESC::Buffer(vBufferSize),
        // we will start this heap in the copy destination state since we will
        // copy data from the upload heap to this heap
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,  // optimized clear value must be null for this type of
                  // resource. used for render targets and depth/stencil buffers
        &geom.m_VertexBufferAllocation, IID_PPV_ARGS(&vertexBufferPtr)));

    geom.m_VertexBuffer.Attach(vertexBufferPtr);

    // we can give resource heaps a name so when we debug with the graphics
    // debugger we know what resource we are looking at
    geom.m_VertexBuffer->SetName(L"Vertex Buffer Resource Heap");
    geom.m_VertexBufferAllocation->SetName(L"Vertex Buffer Resource Heap");

    // create upload heap
    // Upload heaps are used to upload data to the GPU. CPU can write to it, GPU
    // can read from it. We will upload the vertex buffer using this heap to the
    // default heap
    D3D12MA::ALLOCATION_DESC vBufferUploadAllocDesc = {};
    vBufferUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> vBufferUploadHeap;
    CHECK_HR(g_Allocator->CreateResource(
        &vBufferUploadAllocDesc, &CD3DX12_RESOURCE_DESC::Buffer(vBufferSize),
        // GPU will read from this buffer and copy its contents to the default
        // heap
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        &geom.vBufferUploadHeapAllocation, IID_PPV_ARGS(&vBufferUploadHeap)));

    vBufferUploadHeap->SetName(L"Vertex Buffer Upload Resource Heap");
    geom.vBufferUploadHeapAllocation->SetName(
        L"Vertex Buffer Upload Resource Heap");

    // store vertex buffer in upload heap
    D3D12_SUBRESOURCE_DATA vertexData = {};
    vertexData.pData = reinterpret_cast<BYTE*>(mesh->vertices.data());
    vertexData.RowPitch = vBufferSize;
    vertexData.SlicePitch = vBufferSize;

    // we are now creating a command with the command list to copy the data from
    // the upload heap to the default heap
    UINT64 r =
        UpdateSubresources(g_CommandList.Get(), geom.m_VertexBuffer.Get(),
                           vBufferUploadHeap.Get(), 0, 0, 1, &vertexData);
    assert(r);

    // transition the vertex buffer data from copy destination state to vertex
    // buffer state
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        geom.m_VertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    g_CommandList->ResourceBarrier(1, &barrier);

    // create a vertex buffer
    geom.m_VertexBufferView.BufferLocation =
        geom.m_VertexBuffer->GetGPUVirtualAddress();
    geom.m_VertexBufferView.StrideInBytes = sizeof(Vertex);
    geom.m_VertexBufferView.SizeInBytes = (UINT)vBufferSize;
  }

  // Index Buffer
  {
    // m_CubeIndexCount = mesh->header.numIndices;
    UINT64 iBufferSize = mesh->IndexBufferSize();

    // create default heap to hold index buffer
    D3D12MA::ALLOCATION_DESC indexBufferAllocDesc = {};
    indexBufferAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    CHECK_HR(g_Allocator->CreateResource(
        &indexBufferAllocDesc, &CD3DX12_RESOURCE_DESC::Buffer(iBufferSize),
        D3D12_RESOURCE_STATE_COPY_DEST,  // start in the copy destination state
        nullptr,  // optimized clear value must be null for this type of
                  // resource
        &geom.m_IndexBufferAllocation, IID_PPV_ARGS(&geom.m_IndexBuffer)));

    // we can give resource heaps a name so when we debug with the graphics
    // debugger we know what resource we are looking at
    geom.m_IndexBuffer->SetName(L"Index Buffer Resource Heap");
    geom.m_IndexBufferAllocation->SetName(
        L"Index Buffer Resource Heap Allocation");

    // create upload heap to upload index buffer
    D3D12MA::ALLOCATION_DESC iBufferUploadAllocDesc = {};
    iBufferUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> iBufferUploadHeap;
    CHECK_HR(g_Allocator->CreateResource(
        &iBufferUploadAllocDesc, &CD3DX12_RESOURCE_DESC::Buffer(iBufferSize),
        // GPU will read from this buffer and copy its contents to the default
        // heap
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        &geom.iBufferUploadHeapAllocation, IID_PPV_ARGS(&iBufferUploadHeap)));

    CHECK_HR(iBufferUploadHeap->SetName(L"Index Buffer Upload Resource Heap"));
    geom.iBufferUploadHeapAllocation->SetName(
        L"Index Buffer Upload Resource Heap Allocation");

    // store index buffer in upload heap
    D3D12_SUBRESOURCE_DATA indexData = {};
    indexData.pData = reinterpret_cast<BYTE*>(mesh->indices.data());
    indexData.RowPitch = iBufferSize;
    indexData.SlicePitch = iBufferSize;

    // we are now creating a command with the command list to copy the data from
    // the upload heap to the default heap
    UINT64 r = UpdateSubresources(g_CommandList.Get(), geom.m_IndexBuffer.Get(),
                                  iBufferUploadHeap.Get(), 0, 0, 1, &indexData);
    assert(r);

    // transition the index buffer data from copy destination state to index
    // buffer state
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        geom.m_IndexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_INDEX_BUFFER);
    g_CommandList->ResourceBarrier(1, &barrier);

    // create a index buffer view
    geom.m_IndexBufferView.BufferLocation =
        geom.m_IndexBuffer->GetGPUVirtualAddress();
    geom.m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
    geom.m_IndexBufferView.SizeInBytes = (UINT)iBufferSize;
  }

  g_Geometries[mesh->name] = geom;

  return &g_Geometries[mesh->name];
}

static Texture* CreateTexture(std::string name)
{
  Texture tex;
  tex.Read(name);

  D3D12_RESOURCE_DESC textureDesc =
      CD3DX12_RESOURCE_DESC::Tex2D(tex.Format(), tex.Width(), tex.Height());
  textureDesc.MipLevels = 1;

  D3D12MA::ALLOCATION_DESC textureAllocDesc = {};
  textureAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
  CHECK_HR(g_Allocator->CreateResource(
      &textureAllocDesc, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr,  // pOptimizedClearValue
      &tex.textureAllocation, IID_PPV_ARGS(&tex.resource)));

  std::wstring wName = ConvertToWstring(name);
  tex.resource->SetName(wName.c_str());
  tex.textureAllocation->SetName(wName.c_str());

  UINT64 textureUploadBufferSize;
  g_Device->GetCopyableFootprints(&textureDesc,
                                  0,        // FirstSubresource
                                  1,        // NumSubresources
                                  0,        // BaseOffset
                                  nullptr,  // pLayouts
                                  nullptr,  // pNumRows
                                  nullptr,  // pRowSizeInBytes
                                  &textureUploadBufferSize);  // pTotalBytes

  D3D12MA::ALLOCATION_DESC textureUploadAllocDesc = {};
  textureUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

  ComPtr<ID3D12Resource> textureUpload;
  CHECK_HR(g_Allocator->CreateResource(
      &textureUploadAllocDesc,
      &CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,  // pOptimizedClearValue
      &tex.textureUploadAllocation, IID_PPV_ARGS(&textureUpload)));

  textureUpload->SetName(L"textureUpload");
  tex.textureUploadAllocation->SetName(L"textureUpload");

  D3D12_SUBRESOURCE_DATA textureSubresourceData = {};
  textureSubresourceData.pData = tex.pixels.data();
  textureSubresourceData.RowPitch = tex.BytesPerRow();
  textureSubresourceData.SlicePitch = tex.ImageSize();

  UpdateSubresources(g_CommandList.Get(), tex.resource.Get(),
                     textureUpload.Get(), 0, 0, 1, &textureSubresourceData);

  auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
      tex.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  g_CommandList->ResourceBarrier(1, &barrier);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = textureDesc.Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  g_SrvDescHeapAlloc.Alloc(&tex.srvCpuDescHandle, &tex.srvGpuDescHandle);

  g_Device->CreateShaderResourceView(tex.resource.Get(), &srvDesc,
                                     tex.srvCpuDescHandle);

  g_Textures[name] = tex;

  return &g_Textures[name];
}
}  // namespace Renderer
