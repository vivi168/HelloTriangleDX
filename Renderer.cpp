#include "stdafx.h"

#include "Renderer.h"

#include "Win32Application.h"

#include "Camera.h"
#include "Mesh.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Renderer
{
enum class PSO { Basic, BasicMS, Skinned, ColliderSurface };

namespace RootParameter
{
enum Slots : size_t { FrameConstants = 0, PerModelConstants, BoneTransforms, MaterialConstants, PerMeshConstants, Count };
}

struct Scene {
  struct SceneNode {
    Model3D* model;
    size_t cbIndex; // TODO: this is really an offset in the buffer
    std::vector<size_t> bonesIndices; // TODO: these are offsets in the buffer
  };

  std::list<SceneNode> nodes;
  Camera* camera;
};

// TODO: rename this ?
struct ModelConstantBuffer {
  XMFLOAT4X4 WorldViewProj;
  XMFLOAT4X4 WorldMatrix;
  XMFLOAT4X4 NormalMatrix;
};

struct DescriptorHeapListAllocator {
  void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
  {
    assert(m_Heap == nullptr && m_FreeIndices.empty());
    m_Heap = heap;

    D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
    m_HeapType = desc.Type;
    m_HeapStartCpu = m_Heap->GetCPUDescriptorHandleForHeapStart();
    m_HeapStartGpu = m_Heap->GetGPUDescriptorHandleForHeapStart();
    m_HeapHandleIncrement =
        device->GetDescriptorHandleIncrementSize(m_HeapType);

    for (UINT i = 0; i < desc.NumDescriptors; i++) m_FreeIndices.push_back(i);
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

    UINT idx = m_FreeIndices.front();
    m_FreeIndices.pop_front();

    outCpuDescHandle->ptr = m_HeapStartCpu.ptr + (idx * m_HeapHandleIncrement);
    outGpuDescHandle->ptr = m_HeapStartGpu.ptr + (idx * m_HeapHandleIncrement);
  }

  UINT Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpuDescHandle)
  {
    assert(m_FreeIndices.size() > 0);

    UINT idx = m_FreeIndices.front();
    m_FreeIndices.pop_front();

    outCpuDescHandle->ptr = m_HeapStartCpu.ptr + (idx * m_HeapHandleIncrement);

    return idx;
  }

  void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle)
  {
    UINT cpuIdx = static_cast<UINT>((cpuDescHandle.ptr - m_HeapStartCpu.ptr) /
                                    m_HeapHandleIncrement);
    UINT gpuIdx = static_cast<UINT>((gpuDescHandle.ptr - m_HeapStartGpu.ptr) /
                                    m_HeapHandleIncrement);

    assert(cpuIdx == gpuIdx);
    m_FreeIndices.push_front(cpuIdx);
  }

  void Free(UINT index) { m_FreeIndices.push_front(index); }

private:
  ID3D12DescriptorHeap* m_Heap = nullptr;
  D3D12_DESCRIPTOR_HEAP_TYPE m_HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
  D3D12_CPU_DESCRIPTOR_HANDLE m_HeapStartCpu;
  D3D12_GPU_DESCRIPTOR_HANDLE m_HeapStartGpu;
  UINT m_HeapHandleIncrement;
  std::deque<UINT> m_FreeIndices;
};

struct HeapDescriptor
{
  UINT index;
  D3D12_CPU_DESCRIPTOR_HANDLE handle;

  void Alloc(DescriptorHeapListAllocator& allocator)
  {
    index = allocator.Alloc(&handle);
  }
};

struct HeapResource {
  ID3D12Resource* Resource() const { return resource.Get(); };

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(size_t offset = 0) const
  {
    return resource->GetGPUVirtualAddress() + offset;
  }

  void AllocDescriptor(DescriptorHeapListAllocator& allocator)
  {
    descriptor.Alloc(allocator);
  }

  UINT DescriptorIndex() const { return descriptor.index; }

  D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle() const { return descriptor.handle; }

  void CreateResource(D3D12MA::Allocator* allocator,
                      const D3D12MA::ALLOCATION_DESC* allocDesc,
                      const D3D12_RESOURCE_DESC* pResourceDesc,
                      D3D12_RESOURCE_STATES InitialResourceState =
                          D3D12_RESOURCE_STATE_GENERIC_READ,
                      const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr)
  {
    CHECK_HR(allocator->CreateResource(
        allocDesc, pResourceDesc, InitialResourceState, pOptimizedClearValue,
        &allocation, IID_PPV_ARGS(&resource)));
  }

  void Map() { CHECK_HR(resource->Map(0, &EMPTY_RANGE, &address)); }

  void Copy(size_t offset, const void* data, size_t size)
  {
    memcpy((BYTE*)address + offset, data, size);
  }

  D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES stateBefore,
                                    D3D12_RESOURCE_STATES stateAfter)
  {
    return CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), stateBefore,
                                                stateAfter);
  }

  void SetName(std::string name)
  {
    std::wstring bufName = ConvertToWstring(name);
    std::wstring allocName = bufName + L" (Allocation)";

    resource->SetName(bufName.c_str());
    allocation->SetName(allocName.c_str());
  }

  void SetName(std::string name, int index)
  {
    std::wstring bufName = ConvertToWstring(name) + L" " + std::to_wstring(index);
    std::wstring allocName = bufName + L" (Allocation)";

    resource->SetName(bufName.c_str());
    allocation->SetName(allocName.c_str());
  }

  void Reset()
  {
    resource.Reset();
    allocation->Release();
    allocation = nullptr;
  }

private:
  ComPtr<ID3D12Resource> resource;
  HeapDescriptor descriptor;
  D3D12MA::Allocation* allocation;
  void* address;
};

struct UploadedHeapResource {
  void CreateResources(D3D12MA::Allocator* allocator,
                       const D3D12_RESOURCE_DESC* pResourceDesc,
                       const D3D12_RESOURCE_DESC* pResourceUploadDesc,
                       D3D12_RESOURCE_STATES InitialResourceState =
                           D3D12_RESOURCE_STATE_COMMON,
                       const D3D12_CLEAR_VALUE* pOptimizedClearValue = nullptr)
  {
    // create default heap
    // Default heap is memory on the GPU. Only the GPU has access to this
    // memory. To get data into this heap, we will have to upload the data using
    // an upload heap
    D3D12MA::CALLOCATION_DESC allocDesc =
        D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_DEFAULT};
    buffer.CreateResource(allocator, &allocDesc, pResourceDesc,
                          InitialResourceState, pOptimizedClearValue);
    // create upload heap
    // Upload heaps are used to upload data to the GPU. CPU can write to it, GPU
    // can read from it. We will upload the buffer using this heap to the
    // default heap
    D3D12MA::CALLOCATION_DESC uploadAllocDesc =
        D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_UPLOAD};
    uploadBuffer.CreateResource(allocator, &uploadAllocDesc,
                                pResourceUploadDesc);
  }

  void Upload(ID3D12GraphicsCommandList* pCmdList, D3D12_SUBRESOURCE_DATA* data)
  {
    UINT64 r = UpdateSubresources(pCmdList, Resource(), UploadResource(), 0, 0,
                                  1, data);
    assert(r);
  }

  D3D12_RESOURCE_BARRIER Transition(D3D12_RESOURCE_STATES stateAfter)
  {
    return buffer.Transition(D3D12_RESOURCE_STATE_COPY_DEST, stateAfter);
  }

  ID3D12Resource* Resource() const { return buffer.Resource(); };

  D3D12_GPU_VIRTUAL_ADDRESS GpuAddress(size_t offset = 0) const
  {
    return buffer.GpuAddress(offset);
  }

  void AllocDescriptor(DescriptorHeapListAllocator& allocator)
  {
    buffer.AllocDescriptor(allocator);
  }

  UINT DescriptorIndex() const { return buffer.DescriptorIndex(); }

  D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHandle() const
  {
    return buffer.DescriptorHandle();
  }

  void SetName(std::string name) { buffer.SetName(name); }

  void Reset() { buffer.Reset(); }

  void ResetUpload() { uploadBuffer.Reset(); }

private:
  HeapResource buffer;
  HeapResource uploadBuffer;

  ID3D12Resource* UploadResource() const
  {
    return uploadBuffer.Resource();
  };
};

struct Geometry {
  UploadedHeapResource m_VertexBuffer;

  // TODO: bindful. soon to be useless.
  D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;

  UploadedHeapResource m_IndexBuffer;
  D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

  // meshlets
  UploadedHeapResource m_MeshletBuffer;
  UploadedHeapResource m_MeshletIndexBuffer;
  UploadedHeapResource m_MeshletPrimitiveBuffer;
  UploadedHeapResource m_MeshletMaterialBuffer; // TODO: for now contains just a descriptor index of a texture

  void Reset()
  {
    m_VertexBuffer.Reset();
    m_IndexBuffer.Reset();

    m_MeshletBuffer.Reset();
    m_MeshletIndexBuffer.Reset();
    m_MeshletPrimitiveBuffer.Reset();
    m_MeshletMaterialBuffer.Reset();
  }

  void ResetUploadBuffers()
  {
    m_VertexBuffer.ResetUpload();
    m_IndexBuffer.ResetUpload();

    m_MeshletBuffer.ResetUpload();
    m_MeshletIndexBuffer.ResetUpload();
    m_MeshletPrimitiveBuffer.ResetUpload();
    m_MeshletMaterialBuffer.ResetUpload();
  }
};

struct Texture {
  struct {
    uint16_t width;
    uint16_t height;
  } header;

  std::vector<uint8_t> pixels;
  std::string name;

  UploadedHeapResource m_Buffer;

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

  void Reset() { m_Buffer.Reset(); }
  void ResetUploadBuffer() { m_Buffer.ResetUpload(); }

  DXGI_FORMAT Format() const { return DXGI_FORMAT_R8G8B8A8_UNORM; }

  uint32_t BytesPerPixel() const { return 4; }

  uint32_t Width() const { return header.width; }

  uint32_t Height() const { return header.height; }

  size_t BytesPerRow() const { return Width() * BytesPerPixel(); }

  size_t ImageSize() const { return Height() * BytesPerRow(); }
};

struct FrameContext {
  struct {
    float time;
    float deltaTime;
  } frameConstants;

  static constexpr size_t frameConstantsSize = sizeof(frameConstants) / sizeof(UINT32);

  ComPtr<ID3D12Resource> renderTarget;

  ComPtr<ID3D12CommandAllocator> commandAllocator;
  ComPtr<ID3D12Fence> fence;
  UINT64 fenceValue;

  HeapResource perModelConstants;
  HeapResource boneTransformMatrices;

  void Reset() {
    perModelConstants.Reset();
    boneTransformMatrices.Reset();

    renderTarget.Reset();
    commandAllocator.Reset();
    fence.Reset();
  }
};

// ========== Constants

#define ENABLE_DEBUG_LAYER true

static const bool ENABLE_CPU_ALLOCATION_CALLBACKS = true;
static const bool ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT = true;
static void* const CUSTOM_ALLOCATION_PRIVATE_DATA =
    (void*)(uintptr_t)0xDEADC0DE;

static const size_t OBJECT_CB_ALIGNED_SIZE =
    AlignUp<size_t>(sizeof(ModelConstantBuffer), 256);
static const size_t FRAME_BUFFER_COUNT = 3;

static const UINT PRESENT_SYNC_INTERVAL = 1;
static const UINT NUM_DESCRIPTORS_PER_HEAP = 1024;

static const DXGI_FORMAT RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
static const D3D_FEATURE_LEVEL FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_1;

// ========== Static functions declarations

static void InitD3D();
static void InitFrameResources();
static void WaitForFrame(FrameContext* ctx);
static void WaitGPUIdle(size_t frameIndex);
static std::wstring GetAssetFullPath(LPCWSTR assetName);
static void PrintAdapterInformation(IDXGIAdapter1* adapter);
template <typename T> static void LoadMesh3D(Mesh3D<T>* mesh);
template <typename T> static Geometry* CreateGeometry(Mesh3D<T>* mesh);
static Texture* CreateTexture(std::string name);

// ========== Global variables

static size_t g_CbNextIndex = 0; // TODO: rename as offset
                                 // TODO: add a struct to allocate / free indices
static size_t g_BonesNextIndex = 0; // TODO: rename as offset
                                    // use bindless for bone matrices?

static UINT g_Width;
static UINT g_Height;
static float g_AspectRatio;
static bool g_Raster = true;

static std::wstring g_Title;
static std::wstring g_AssetsPath;

// Pipeline objects
static DXGIUsage* g_DXGIUsage = nullptr;
static ComPtr<IDXGIAdapter1> g_Adapter;

static ComPtr<ID3D12Device2> g_Device;
static DXGI_ADAPTER_DESC1 g_AdapterDesc;
static ComPtr<D3D12MA::Allocator> g_Allocator;
// Used only when ENABLE_CPU_ALLOCATION_CALLBACKS
static D3D12MA::ALLOCATION_CALLBACKS g_AllocationCallbacks;

// swapchain used to switch between render targets
static ComPtr<IDXGISwapChain3> g_SwapChain;
// container for command lists
static ComPtr<ID3D12CommandQueue> g_CommandQueue;
static ComPtr<ID3D12GraphicsCommandList6> g_CommandList;

static FrameContext g_FrameContext[FRAME_BUFFER_COUNT];
static UINT g_FrameIndex;
static HANDLE g_FenceEvent;

// Resources
static ComPtr<ID3D12DescriptorHeap> g_SrvDescriptorHeap;
static DescriptorHeapListAllocator g_SrvDescHeapAlloc;

static ComPtr<ID3D12DescriptorHeap> g_RtvDescriptorHeap;
static UINT g_RtvDescriptorSize;

static HeapResource g_DepthStencilBuffer;
static ComPtr<ID3D12DescriptorHeap> g_DepthStencilDescriptorHeap;

// PSO
static std::unordered_map<PSO, ComPtr<ID3D12PipelineState>>
    g_PipelineStateObjects;
static ComPtr<ID3D12RootSignature> g_RootSignature;

static std::unordered_map<std::string, Geometry> g_Geometries;
static std::unordered_map<std::string, Texture> g_Textures;
static Scene g_Scene;

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
  auto cmdAllocator = g_FrameContext[g_FrameIndex].commandAllocator.Get();
  CHECK_HR(g_CommandList->Reset(cmdAllocator, NULL));

  for (auto node : g_Scene.nodes) {
    for (auto mesh : node.model->meshes) LoadMesh3D(mesh);
    for (auto mesh : node.model->skinnedMeshes) LoadMesh3D(mesh);
  }

  g_CommandList->Close();

  ID3D12CommandList* ppCommandLists[] = {g_CommandList.Get()};
  g_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  WaitGPUIdle(g_FrameIndex);

  // Release upload buffers
  for (auto& [k, tex] : g_Textures) {
    tex.ResetUploadBuffer();
  }

  for (auto& [k, geom] : g_Geometries) {
    geom.ResetUploadBuffers();
  }
}

void Update(float time, float dt)
{
  auto ctx = &g_FrameContext[g_FrameIndex];

  // Per frame root constants
  {
    ctx->frameConstants.time = time;
    ctx->frameConstants.deltaTime = dt;
  }

  // Per object constant buffer
  {
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(
        45.f * (XM_PI / 180.f), g_AspectRatio, 0.1f, 1000.f);

    XMMATRIX view = g_Scene.camera->LookAt();
    XMMATRIX viewProjection = view * projection;

    for (auto& node : g_Scene.nodes) {
      ModelConstantBuffer cb;

      auto model = node.model;

      if (model->currentAnimation) {
        model->currentAnimation->Update(dt);
        size_t i = 0;
        // TODO: don't loop skinnedMesh but loop unique skins to avoid computing same thing twice
        for (auto skinnedMesh : model->skinnedMeshes) {
          std::vector<XMFLOAT4X4> matrices = model->currentAnimation->BoneTransforms(skinnedMesh->skin);

          auto bonesIndex = node.bonesIndices[i++];
          ctx->boneTransformMatrices.Copy(bonesIndex, matrices.data(),
                                          sizeof(XMFLOAT4X4) * matrices.size());
        }
      }  // else identity matrices ?

      XMMATRIX worldViewProjection = model->WorldMatrix() * viewProjection;
      XMStoreFloat4x4(&cb.WorldViewProj,
                      XMMatrixTranspose(worldViewProjection));
      XMStoreFloat4x4(&cb.WorldMatrix,
                      XMMatrixTranspose(model->WorldMatrix()));

      XMMATRIX normalMatrix =
          XMMatrixInverse(nullptr, model->WorldMatrix());
      XMStoreFloat4x4(&cb.NormalMatrix, normalMatrix);

      ctx->perModelConstants.Copy(node.cbIndex, &cb, sizeof(cb));
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
  auto ctx = &g_FrameContext[g_FrameIndex];

  // We have to wait for the gpu to finish with the command allocator before we
  // reset it
  WaitForFrame(ctx);

  // increment fenceValue for next frame
  ctx->fenceValue++;

  // we can only reset an allocator once the gpu is done with it. Resetting an
  // allocator frees the memory that the command list was stored in
  CHECK_HR(ctx->commandAllocator->Reset());

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
  CHECK_HR(g_CommandList->Reset(ctx->commandAllocator.Get(), NULL));

  // here we start recording commands into the g_CommandList (which all the
  // commands will be stored in the g_CommandAllocators)

  // transition the "g_FrameIndex" render target from the present state to the
  // render target state so the command list draws to it starting from here
  auto presentToRenderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
      ctx->renderTarget.Get(), D3D12_RESOURCE_STATE_PRESENT,
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
      D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  // Clear the render target by using the ClearRenderTargetView command
  if (g_Raster) {
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  } else {
    const float clearColor[] = {0.6f, 0.8f, 0.4f, 1.0f};
    g_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  }

  std::array descriptorHeaps{g_SrvDescriptorHeap.Get()};
  g_CommandList->SetDescriptorHeaps(descriptorHeaps.size(), descriptorHeaps.data());

  g_CommandList->SetGraphicsRootSignature(g_RootSignature.Get());

  g_CommandList->SetGraphicsRoot32BitConstants(
      RootParameter::FrameConstants, FrameContext::frameConstantsSize, &ctx->frameConstants, 0);

  D3D12_VIEWPORT viewport{0.f, 0.f, (float)g_Width, (float)g_Height, 0.f, 1.f};
  g_CommandList->RSSetViewports(1, &viewport);

  D3D12_RECT scissorRect{0, 0, g_Width, g_Height};
  g_CommandList->RSSetScissorRects(1, &scissorRect);

  for (const auto node : g_Scene.nodes) {
    // static meshes
    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::BasicMS].Get());

    for (auto mesh : node.model->meshes) {
      auto geom = mesh->geometry;
      g_CommandList->SetGraphicsRootConstantBufferView(
          RootParameter::PerModelConstants, ctx->perModelConstants.GpuAddress(node.cbIndex));

      struct {
        UINT vertexBufferId;
        UINT meshletBufferId;
        UINT indexBufferId;
        UINT primBufferId;
        UINT materialBufferId;
      } ronre = {geom->m_VertexBuffer.DescriptorIndex(),
                 geom->m_MeshletBuffer.DescriptorIndex(),
                 geom->m_MeshletIndexBuffer.DescriptorIndex(),
                 geom->m_MeshletPrimitiveBuffer.DescriptorIndex(),
                 geom->m_MeshletMaterialBuffer.DescriptorIndex()};
      g_CommandList->SetGraphicsRoot32BitConstants(
          RootParameter::PerMeshConstants, sizeof(ronre) / sizeof(UINT),
          &ronre, 0);
      g_CommandList->DispatchMesh(mesh->meshlets.size(), 1, 1);
    }

    // skinned meshes
    g_CommandList->SetPipelineState(g_PipelineStateObjects[PSO::Skinned].Get());

    size_t i = 0;
    for (auto mesh : node.model->skinnedMeshes) {
      auto geom = mesh->geometry;
      g_CommandList->IASetVertexBuffers(0, 1, &geom->m_VertexBufferView);
      g_CommandList->IASetIndexBuffer(&geom->m_IndexBufferView);
      g_CommandList->IASetPrimitiveTopology(
          D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

      g_CommandList->SetGraphicsRootConstantBufferView(
          RootParameter::PerModelConstants, ctx->perModelConstants.GpuAddress(node.cbIndex));

      auto bonesIndex = node.bonesIndices[i++];
      g_CommandList->SetGraphicsRootShaderResourceView(
          RootParameter::BoneTransforms,
          ctx->boneTransformMatrices.GpuAddress(bonesIndex));

      for (const auto& subset : mesh->subsets) {
        struct {
          UINT vbIndex;
          UINT vOffset;
          UINT texIndex;
        } ronre = {geom->m_VertexBuffer.DescriptorIndex(), subset.vstart,
                   subset.texture->m_Buffer.DescriptorIndex()};
        g_CommandList->SetGraphicsRoot32BitConstants(
            RootParameter::MaterialConstants, sizeof(ronre) / sizeof(UINT),
            &ronre, 0);
        g_CommandList->DrawIndexedInstanced(subset.count, 1, subset.start,
                                            subset.vstart, 0);
      }
    }
  }

  ImGui::Render();
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_CommandList.Get());

  // transition the "g_FrameIndex" render target from the render target state to
  // the present state. If the debug layer is enabled, you will receive a
  // warning if present is called on the render target when it's not in the
  // present state
  auto renderTargetToPresentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
      ctx->renderTarget.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_PRESENT);
  g_CommandList->ResourceBarrier(1, &renderTargetToPresentBarrier);

  CHECK_HR(g_CommandList->Close());

  // ==========

  // create an array of command lists (only one command list here)
  std::array ppCommandLists{
      static_cast<ID3D12CommandList*>(g_CommandList.Get())};

  // execute the array of command lists
  g_CommandQueue->ExecuteCommandLists(ppCommandLists.size(), ppCommandLists.data());

  // this command goes in at the end of our command queue. we will know when our
  // command queue has finished because the fence value will be set to
  // "fenceValue" from the GPU since the command queue is being executed on
  // the GPU
  CHECK_HR(g_CommandQueue->Signal(ctx->fence.Get(), ctx->fenceValue));

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
    // TODO: Method from FrameContext
    auto ctx = &g_FrameContext[i];
    WaitForFrame(ctx);
    CHECK_HR(g_CommandQueue->Wait(ctx->fence.Get(), ctx->fenceValue));
  }

  // get swapchain out of full screen before exiting
  BOOL fs = false;
  CHECK_HR(g_SwapChain->GetFullscreenState(&fs, NULL));

  if (fs) g_SwapChain->SetFullscreenState(false, NULL);

  WaitGPUIdle(0);

  for (auto& [k, tex] : g_Textures) {
    // TODO: do this from Texture#Reset() ?
    g_SrvDescHeapAlloc.Free(tex.m_Buffer.DescriptorIndex());
    tex.Reset();
  }

  for (auto& [k, geom] : g_Geometries) {
    // TODO: should free geometry Descriptor Indices
    geom.Reset();
  }

  g_PipelineStateObjects[PSO::Basic].Reset();
  g_PipelineStateObjects[PSO::BasicMS].Reset();
  g_PipelineStateObjects[PSO::Skinned].Reset();
  g_RootSignature.Reset();

  CloseHandle(g_FenceEvent);
  g_CommandList.Reset();
  g_CommandQueue.Reset();

  g_SrvDescriptorHeap.Reset();
  g_RtvDescriptorHeap.Reset();
  g_DepthStencilBuffer.Reset();
  g_DepthStencilDescriptorHeap.Reset();

  for (size_t i = FRAME_BUFFER_COUNT; i--;) {
    g_FrameContext[i].Reset();
  }

  PrintStatsString();

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

  Scene::SceneNode node;
  node.model = model;
  node.cbIndex = cbIndex;

  for (auto mesh : model->skinnedMeshes) {
    size_t boneIndex = g_BonesNextIndex;
    g_BonesNextIndex += mesh->SkinMatricesSize();
    node.bonesIndices.push_back(boneIndex);
  }

  g_Scene.nodes.push_back(node);
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
    ID3D12Device2* device = nullptr;
    CHECK_HR(D3D12CreateDevice(g_Adapter.Get(), FEATURE_LEVEL,
                               IID_PPV_ARGS(&device)));
    g_Device.Attach(device);

    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    CHECK_HR(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                           &options5, sizeof(options5)));
    assert(options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);

    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
    CHECK_HR(g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,
                                           &options7, sizeof(options7)));
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
      g_FrameContext[i].commandAllocator.Attach(commandAllocator);
    }

    // create the command list with the first allocator
    CHECK_HR(
        g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    g_FrameContext[0].commandAllocator.Get(),
                                    NULL, IID_PPV_ARGS(&g_CommandList)));

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
      g_FrameContext[i].fence.Attach(fence);
      g_FrameContext[i].fenceValue = 0;
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

  // RTV
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
      g_FrameContext[i].renderTarget.Attach(res);

      // then we "create" a render target view which binds the swap chain buffer
      // (ID3D12Resource[n]) to the rtv handle
      g_Device->CreateRenderTargetView(g_FrameContext[i].renderTarget.Get(),
                                       nullptr, rtvHandle);

      // we increment the rtv handle by the rtv descriptor size we got above
      rtvHandle.ptr += g_RtvDescriptorSize;
    }
  }

  // DSV
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

    D3D12MA::CALLOCATION_DESC depthStencilAllocDesc =
        D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_DEFAULT};

    D3D12_RESOURCE_DESC depthStencilResourceDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(DEPTH_STENCIL_FORMAT, g_Width, g_Height);
    depthStencilResourceDesc.MipLevels = 1;
    depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    g_DepthStencilBuffer.CreateResource(
        g_Allocator.Get(), &depthStencilAllocDesc, &depthStencilResourceDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthOptimizedClearValue);

    g_DepthStencilBuffer.SetName("Depth Stencil Buffer");

    D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
    depthStencilDesc.Format = DEPTH_STENCIL_FORMAT;
    depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
    g_Device->CreateDepthStencilView(
        g_DepthStencilBuffer.Resource(), &depthStencilDesc,
        g_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
  }

  // CBV_SRV_UAV descriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = NUM_DESCRIPTORS_PER_HEAP;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  CHECK_HR(g_Device->CreateDescriptorHeap(&heapDesc,
                                          IID_PPV_ARGS(&g_SrvDescriptorHeap)));

  g_SrvDescHeapAlloc.Create(g_Device.Get(), g_SrvDescriptorHeap.Get());

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
  initInfo.SrvDescriptorHeap = g_SrvDescriptorHeap.Get();
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

  // Root Signature
  {
    // Root parameters
    // Applications should sort entries in the root signature from most frequently changing to least.
    CD3DX12_ROOT_PARAMETER rootParameters[RootParameter::Count] = {};
    rootParameters[RootParameter::FrameConstants].InitAsConstants(FrameContext::frameConstantsSize, 0); // b0
    rootParameters[RootParameter::PerModelConstants].InitAsConstantBufferView(1);  // b1
    rootParameters[RootParameter::BoneTransforms].InitAsShaderResourceView(0); // t0
    rootParameters[RootParameter::MaterialConstants].InitAsConstants(3, 2);  // b2 // TODO: use variable (materialConstantsSize) + TODO reorder root signature
    rootParameters[RootParameter::PerMeshConstants].InitAsConstants(5, 3);  // b3 // TODO: rework root signature.

    // Static sampler
    CD3DX12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
    staticSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_POINT);

    // Root Signature
    D3D12_ROOT_SIGNATURE_FLAGS flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc(
        RootParameter::Count, rootParameters, 1, staticSamplers, flags);

    ID3DBlob* signatureBlobPtr;
    CHECK_HR(D3D12SerializeVersionedRootSignature(&rootSignatureDesc,
                                                  &signatureBlobPtr, nullptr));

    ID3D12RootSignature* rootSignature = nullptr;
    CHECK_HR(g_Device->CreateRootSignature(
        0, signatureBlobPtr->GetBufferPointer(),
        signatureBlobPtr->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
    g_RootSignature.Attach(rootSignature);
  }

  // per object CB
  {
    D3D12MA::CALLOCATION_DESC allocDesc =
        D3D12MA::CALLOCATION_DESC{D3D12_HEAP_TYPE_UPLOAD};

    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(1024 * 64);

    for (size_t i = 0; i < FRAME_BUFFER_COUNT; i++) {
      auto ctx = &g_FrameContext[i];

      // size of the resource heap. Must be a multiple of 64KB for
      // single-textures and constant buffers
      ctx->perModelConstants.CreateResource(
          g_Allocator.Get(), &allocDesc,
          &bufferDesc);  // 1024 * 64 bytes = 64KB
      ctx->perModelConstants.SetName("Per Model Constant Buffer ", i);
      ctx->perModelConstants.Map();

      ctx->boneTransformMatrices.CreateResource(g_Allocator.Get(), &allocDesc,
                                                &bufferDesc);
      ctx->boneTransformMatrices.SetName("Bone Matrices Constant Buffer ", i);
      ctx->boneTransformMatrices.Map();
    }
  }

  // Pixel Shader
  auto pixelShaderBlob = ReadData(GetAssetFullPath(L"DefaultPS.cso").c_str());
  D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob.data(),
                                       pixelShaderBlob.size()};

  // Pipeline State for static objects
  {
    // Vertex Shader
    auto vertexShaderBlob = ReadData(GetAssetFullPath(L"DefaultVS.cso").c_str());
    D3D12_SHADER_BYTECODE vertexShader = {vertexShaderBlob.data(),
                                          vertexShaderBlob.size()};

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
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.VS = vertexShader;
    psoDesc.PS = pixelShader;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;  // we are only binding one render target
                                   // TODO: for G buffer here set 4 RT
    psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT;
    psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc = DefaultSampleDesc();

    // create the pso
    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::Basic].Attach(pipelineStateObject);
  }

  // Mesh Shader Pipeline State for static objects
  {
    // Mesh Shader
    auto meshShaderBlob = ReadData(GetAssetFullPath(L"MeshletMS.cso").c_str());
    D3D12_SHADER_BYTECODE meshShader = {meshShaderBlob.data(),
                                        meshShaderBlob.size()};

    auto pixelShaderBlob = ReadData(GetAssetFullPath(L"MeshletPS.cso").c_str());
    D3D12_SHADER_BYTECODE pixelShader = {pixelShaderBlob.data(),
                                         pixelShaderBlob.size()};

    D3DX12_MESH_SHADER_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.MS = meshShader;
    psoDesc.PS = pixelShader;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT;
    psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc = DefaultSampleDesc();

    auto psoStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(psoDesc);
    D3D12_PIPELINE_STATE_STREAM_DESC streamDesc;
    streamDesc.pPipelineStateSubobjectStream = &psoStream;
    streamDesc.SizeInBytes = sizeof(psoStream);

    // create the pso
    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreatePipelineState(&streamDesc,
                                           IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::BasicMS].Attach(pipelineStateObject);
  }

  // Pipeline State for skinned objects
  {
    // Vertex Shader
    auto vertexShaderBlob = ReadData(GetAssetFullPath(L"DefaultSkinnedVS.cso").c_str());
    D3D12_SHADER_BYTECODE vertexShader = {vertexShaderBlob.data(),
                                          vertexShaderBlob.size()};

    // Input Layout
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
        {"WEIGHTS", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"BONEINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT, 0,
         D3D12_APPEND_ALIGNED_ELEMENT,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout.NumElements = _countof(inputLayout);
    psoDesc.InputLayout.pInputElementDescs = inputLayout;
    psoDesc.pRootSignature = g_RootSignature.Get();
    psoDesc.VS = vertexShader;
    psoDesc.PS = pixelShader;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT;
    psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc = DefaultSampleDesc();

    // Create the PSO
    ID3D12PipelineState* pipelineStateObject;
    CHECK_HR(g_Device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
    g_PipelineStateObjects[PSO::Skinned].Attach(pipelineStateObject);
  }
}

// wait until gpu is finished with command list
static void WaitForFrame(FrameContext* ctx)
{
  // if the current fence value is still less than "fenceValue", then we
  // know the GPU has not finished executing the command queue since it has
  // not reached the "g_CommandQueue->Signal(fence, fenceValue)" command
  if (ctx->fence->GetCompletedValue() < ctx->fenceValue) {
    // we have the fence create an event which is signaled once the
    // fence's current value is "fenceValue"
    CHECK_HR(ctx->fence->SetEventOnCompletion(ctx->fenceValue, g_FenceEvent));

    // We will wait until the fence has triggered the event that it's
    // current value has reached "fenceValue". once it's value has reached
    // "fenceValue", we know the command queue has finished executing
    WaitForSingleObject(g_FenceEvent, INFINITE);
  }
}

static void WaitGPUIdle(size_t frameIndex)
{
  auto ctx = &g_FrameContext[frameIndex];

  ctx->fenceValue++;
  CHECK_HR(g_CommandQueue->Signal(ctx->fence.Get(), ctx->fenceValue));
  WaitForFrame(ctx);
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

template <typename T>
static void LoadMesh3D(Mesh3D<T>* mesh)
{
  // first loop subsets and create textures (and soon to be materials)
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

  // then we can create geometry. and have a buffer containing a descriptor
  // index per meshlet to a material
  auto geomNeedle = g_Geometries.find(mesh->name);
  if (geomNeedle == std::end(g_Geometries)) {
    printf("CREATE GEOMETRY %s\n", mesh->name.c_str());
    mesh->geometry = CreateGeometry(mesh);
  } else {
    printf("GEOMETRY ALREADY EXISTS %s\n", mesh->name.c_str());
    mesh->geometry = &geomNeedle->second;
  }
}

template <typename T>
static Geometry* CreateGeometry(Mesh3D<T>* mesh)
{
  Geometry geom;

  std::array<D3D12_RESOURCE_BARRIER, 6> postUploadBarriers;

  // Vertex Buffer
  {
    const size_t vBufferSize = mesh->VertexBufferSize();
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(vBufferSize);

    geom.m_VertexBuffer.CreateResources(g_Allocator.Get(), &bufferDesc,
                                        &bufferDesc);

    geom.m_VertexBuffer.SetName("Vertex Buffer: " + mesh->name);

    D3D12_SUBRESOURCE_DATA vertexData = {};
    vertexData.pData = reinterpret_cast<BYTE*>(mesh->vertices.data());
    vertexData.RowPitch = vBufferSize;
    vertexData.SlicePitch = vBufferSize;

    geom.m_VertexBuffer.Upload(g_CommandList.Get(), &vertexData);
    postUploadBarriers[0] = geom.m_VertexBuffer.Transition(
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // create a vertex buffer view
    // TODO: useless when we go full bindless (missing: skinned models)
    geom.m_VertexBufferView.BufferLocation = geom.m_VertexBuffer.GpuAddress();
    geom.m_VertexBufferView.StrideInBytes = sizeof(T);
    geom.m_VertexBufferView.SizeInBytes = (UINT)vBufferSize;

    // create a descriptor for bindless
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = mesh->vertices.size();
    srvDesc.Buffer.StructureByteStride = sizeof(T);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    geom.m_VertexBuffer.AllocDescriptor(g_SrvDescHeapAlloc);
    g_Device->CreateShaderResourceView(geom.m_VertexBuffer.Resource(), &srvDesc,
                                       geom.m_VertexBuffer.DescriptorHandle());
  }

  // Index Buffer (not used by mesh shader pipeline)
  {
    size_t iBufferSize = mesh->IndexBufferSize();
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(iBufferSize);

    geom.m_IndexBuffer.CreateResources(g_Allocator.Get(), &bufferDesc,
                                       &bufferDesc);

    geom.m_IndexBuffer.SetName("Index Buffer: " + mesh->name);

    D3D12_SUBRESOURCE_DATA indexData = {};
    indexData.RowPitch = iBufferSize;
    indexData.SlicePitch = iBufferSize;
    indexData.pData = reinterpret_cast<BYTE*>(mesh->indices.data());

    geom.m_IndexBuffer.Upload(g_CommandList.Get(), &indexData);
    postUploadBarriers[1] =
        geom.m_IndexBuffer.Transition(D3D12_RESOURCE_STATE_INDEX_BUFFER);

    // create a index buffer view
    geom.m_IndexBufferView.BufferLocation = geom.m_IndexBuffer.GpuAddress();
    geom.m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
    geom.m_IndexBufferView.SizeInBytes = (UINT)iBufferSize;
  }

  // Meshlet Buffer
  {
    const size_t bufferSize = mesh->MeshletBufferSize();
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    geom.m_MeshletBuffer.CreateResources(g_Allocator.Get(), &bufferDesc,
                                         &bufferDesc);
    geom.m_MeshletBuffer.SetName("Meshlet Buffer: " + mesh->name);

    D3D12_SUBRESOURCE_DATA data{
        .pData = reinterpret_cast<BYTE*>(mesh->meshlets.data()),
        .RowPitch = static_cast<LONG_PTR>(bufferSize),
        .SlicePitch = static_cast<LONG_PTR>(bufferSize)};

    geom.m_MeshletBuffer.Upload(g_CommandList.Get(), &data);
    postUploadBarriers[2] = geom.m_MeshletBuffer.Transition(
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {.FirstElement = 0,
                   .NumElements = static_cast<UINT>(mesh->meshlets.size()),
                   .StructureByteStride = sizeof(Meshlet),
                   .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};

    geom.m_MeshletBuffer.AllocDescriptor(g_SrvDescHeapAlloc);
    g_Device->CreateShaderResourceView(geom.m_MeshletBuffer.Resource(),
                                       &srvDesc,
                                       geom.m_MeshletBuffer.DescriptorHandle());
  }

  // Meshlet Index Buffer
  {
    const size_t bufferSize = mesh->MeshletIndexBufferSize();
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

    geom.m_MeshletIndexBuffer.CreateResources(g_Allocator.Get(), &bufferDesc,
                                              &bufferDesc);
    geom.m_MeshletIndexBuffer.SetName("Meshlet Index Buffer: " + mesh->name);

    D3D12_SUBRESOURCE_DATA data{
        .pData = reinterpret_cast<BYTE*>(mesh->uniqueVertexIB.data()),
        .RowPitch = static_cast<LONG_PTR>(bufferSize),
        .SlicePitch = static_cast<LONG_PTR>(bufferSize)};

    geom.m_MeshletIndexBuffer.Upload(g_CommandList.Get(), &data);
    postUploadBarriers[3] = geom.m_MeshletIndexBuffer.Transition(
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {.FirstElement = 0,
                   .NumElements =
                       static_cast<UINT>(mesh->MeshletIndexBufferNumElements()),
                   .StructureByteStride = sizeof(UINT),
                   .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};

    geom.m_MeshletIndexBuffer.AllocDescriptor(g_SrvDescHeapAlloc);
    g_Device->CreateShaderResourceView(
        geom.m_MeshletIndexBuffer.Resource(), &srvDesc,
        geom.m_MeshletIndexBuffer.DescriptorHandle());
  }

  // Meshlet Primitive Buffer
  {
    const size_t bufferSize = mesh->MeshletPrimitiveBufferSize();
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    geom.m_MeshletPrimitiveBuffer.CreateResources(g_Allocator.Get(),
                                                  &bufferDesc, &bufferDesc);
    geom.m_MeshletPrimitiveBuffer.SetName("Meshlet Primitive Buffer: " +
                                          mesh->name);

    D3D12_SUBRESOURCE_DATA data{
        .pData = reinterpret_cast<BYTE*>(mesh->primitiveIndices.data()),
        .RowPitch = static_cast<LONG_PTR>(bufferSize),
        .SlicePitch = static_cast<LONG_PTR>(bufferSize)};

    geom.m_MeshletPrimitiveBuffer.Upload(g_CommandList.Get(), &data);
    postUploadBarriers[4] = geom.m_MeshletPrimitiveBuffer.Transition(
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {
            .FirstElement = 0,
            .NumElements = static_cast<UINT>(mesh->primitiveIndices.size()),
            .StructureByteStride = sizeof(MeshletTriangle),
            .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};

    geom.m_MeshletPrimitiveBuffer.AllocDescriptor(g_SrvDescHeapAlloc);
    g_Device->CreateShaderResourceView(
        geom.m_MeshletPrimitiveBuffer.Resource(), &srvDesc,
        geom.m_MeshletPrimitiveBuffer.DescriptorHandle());
  }

  // Meshlet Material Buffer
  {
    // CPU buffer
    std::vector<UINT> meshletMaterials(mesh->meshlets.size());

    for (size_t mi = 0; mi < mesh->meshlets.size(); mi++) {
      auto subset = mesh->meshletSubsetIndices[mi];

      meshletMaterials[mi] = subset->texture->m_Buffer.DescriptorIndex();
    }

    // GPU buffer
    const size_t bufferSize = meshletMaterials.size() * sizeof(UINT);
    auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    geom.m_MeshletMaterialBuffer.CreateResources(g_Allocator.Get(), &bufferDesc,
                                                 &bufferDesc);
    geom.m_MeshletMaterialBuffer.SetName("Meshlet Material Buffer: " +
                                         mesh->name);

    D3D12_SUBRESOURCE_DATA data{
        .pData = reinterpret_cast<BYTE*>(meshletMaterials.data()),
        .RowPitch = static_cast<LONG_PTR>(bufferSize),
        .SlicePitch = static_cast<LONG_PTR>(bufferSize)};

    geom.m_MeshletMaterialBuffer.Upload(g_CommandList.Get(), &data);
    postUploadBarriers[5] = geom.m_MeshletMaterialBuffer.Transition(
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // descriptor
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
        .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
        .Buffer = {.FirstElement = 0,
                   .NumElements = static_cast<UINT>(meshletMaterials.size()),
                   .StructureByteStride = sizeof(UINT),
                   .Flags = D3D12_BUFFER_SRV_FLAG_NONE}};

    geom.m_MeshletMaterialBuffer.AllocDescriptor(g_SrvDescHeapAlloc);
    g_Device->CreateShaderResourceView(
        geom.m_MeshletMaterialBuffer.Resource(), &srvDesc,
        geom.m_MeshletMaterialBuffer.DescriptorHandle());
  }

  g_CommandList->ResourceBarrier(postUploadBarriers.size(),
                                 postUploadBarriers.data());

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

  UINT64 textureUploadBufferSize;
  g_Device->GetCopyableFootprints(&textureDesc,
                                  0,        // FirstSubresource
                                  1,        // NumSubresources
                                  0,        // BaseOffset
                                  nullptr,  // pLayouts
                                  nullptr,  // pNumRows
                                  nullptr,  // pRowSizeInBytes
                                  &textureUploadBufferSize);  // pTotalBytes

  auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(textureUploadBufferSize);
  tex.m_Buffer.CreateResources(g_Allocator.Get(), &textureDesc, &bufferDesc,
                               D3D12_RESOURCE_STATE_COPY_DEST);
  tex.m_Buffer.SetName("Texture: " + name);

  D3D12_SUBRESOURCE_DATA textureSubresourceData = {};
  textureSubresourceData.pData = tex.pixels.data();
  textureSubresourceData.RowPitch = tex.BytesPerRow();
  textureSubresourceData.SlicePitch = tex.ImageSize();

  tex.m_Buffer.Upload(g_CommandList.Get(), &textureSubresourceData);
  auto barrier = tex.m_Buffer.Transition(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  g_CommandList->ResourceBarrier(1, &barrier);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = textureDesc.Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  tex.m_Buffer.AllocDescriptor(g_SrvDescHeapAlloc);
  g_Device->CreateShaderResourceView(tex.m_Buffer.Resource(), &srvDesc,
                                     tex.m_Buffer.DescriptorHandle());

  g_Textures[name] = tex;

  return &g_Textures[name];
}
}  // namespace Renderer
