#pragma once

#include "DXSampleHelper.h"
#include "D3D12MemAlloc.h"

using namespace DirectX;

static const size_t FRAME_BUFFER_COUNT = 3; // number of buffers we want, 2 for double buffering, 3 for tripple buffering

struct GPUSelection
{
    UINT32 Index = UINT32_MAX;
    std::wstring Substring;
};

class DXGIUsage
{
public:
    void Init();
    IDXGIFactory4* GetDXGIFactory() const { return m_DXGIFactory.Get(); }
    void PrintAdapterList() const;
    // If failed, returns null pointer.
    ComPtr<IDXGIAdapter1> CreateAdapter(const GPUSelection& GPUSelection) const;

private:
    ComPtr<IDXGIFactory4> m_DXGIFactory;
};

class Renderer
{
public:
	Renderer(UINT width, UINT height, std::wstring name);

	UINT GetWidth() const { return m_width; }
	UINT GetHeight() const { return m_height; }
	const WCHAR* GetTitle() const { return m_title.c_str(); }

    void InitAdapter(DXGIUsage*, GPUSelection);
	void InitD3D();
	void Update(float);
	void Render();
	void Cleanup();
	void OnKeyDown(WPARAM key);

private:
	void WaitForFrame(size_t frameIndex) // wait until gpu is finished with command list
	{
		// if the current m_Fences value is still less than "m_FenceValues", then we know the GPU has not finished executing
		// the command queue since it has not reached the "m_CommandQueue->Signal(m_Fences, m_FenceValues)" command
		if (m_Fences[frameIndex]->GetCompletedValue() < m_FenceValues[frameIndex])
		{
			// we have the m_Fences create an event which is signaled once the m_Fences's current value is "m_FenceValues"
			CHECK_HR(m_Fences[frameIndex]->SetEventOnCompletion(m_FenceValues[frameIndex], m_FenceEvent));

			// We will wait until the m_Fences has triggered the event that it's current value has reached "m_FenceValues". once it's value
			// has reached "m_FenceValues", we know the command queue has finished executing
			WaitForSingleObject(m_FenceEvent, INFINITE);
		}
	}

	void WaitGPUIdle(size_t frameIndex)
	{
		m_FenceValues[frameIndex]++;
		CHECK_HR(m_CommandQueue->Signal(m_Fences[frameIndex].Get(), m_FenceValues[frameIndex]));
		WaitForFrame(frameIndex);
	}

	// Helper function for resolving the full path of assets.
	std::wstring GetAssetFullPath(LPCWSTR assetName)
	{
		return m_assetsPath + assetName;
	}

	void PrintAdapterInformation(IDXGIAdapter1*);

	UINT m_width;
	UINT m_height;
	float m_aspectRatio;

	std::wstring m_title;
	std::wstring m_assetsPath;

    DXGIUsage* m_DXGIUsage;
	ComPtr<IDXGIAdapter1> m_Adapter;

	ComPtr<ID3D12Device> m_Device;
	DXGI_ADAPTER_DESC1 m_AdapterDesc;
	ComPtr<D3D12MA::Allocator> m_Allocator;

	ComPtr<IDXGISwapChain3> m_SwapChain; // swapchain used to switch between render targets
	ComPtr<ID3D12CommandQueue> m_CommandQueue; // container for command lists
	ComPtr<ID3D12DescriptorHeap> m_RtvDescriptorHeap; // a descriptor heap to hold resources like the render targets
	ComPtr<ID3D12Resource> m_RenderTargets[FRAME_BUFFER_COUNT]; // number of render targets equal to buffer count
	ComPtr<ID3D12CommandAllocator> g_CommandAllocators[FRAME_BUFFER_COUNT]; // we want enough allocators for each buffer * number of threads (we only have one thread)
	ComPtr<ID3D12GraphicsCommandList> g_CommandList; // a command list we can record commands into, then execute them to render the frame
	ComPtr<ID3D12Fence> m_Fences[FRAME_BUFFER_COUNT];    // an object that is locked while our command list is being executed by the gpu. We need as many
	//as we have allocators (more if we want to know when the gpu is finished with an asset)
	HANDLE m_FenceEvent; // a handle to an event when our m_Fences is unlocked by the gpu
	UINT64 m_FenceValues[FRAME_BUFFER_COUNT]; // this value is incremented each frame. each m_Fences will have its own value
	UINT m_FrameIndex; // current rtv we are on
	UINT m_RtvDescriptorSize; // size of the rtv descriptor on the m_Device (all front and back buffers will be the same size)

	ComPtr<ID3D12PipelineState> m_PipelineStateObject;
	ComPtr<ID3D12RootSignature> m_RootSignature;
	ComPtr<ID3D12Resource> m_VertexBuffer;
	D3D12MA::Allocation* m_VertexBufferAllocation;
	ComPtr<ID3D12Resource> m_IndexBuffer;
	D3D12MA::Allocation* m_IndexBufferAllocation;
	D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;
	ComPtr<ID3D12Resource> m_DepthStencilBuffer;
	D3D12MA::Allocation* m_DepthStencilAllocation;
	ComPtr<ID3D12DescriptorHeap> m_DepthStencilDescriptorHeap;

	struct Vertex {
		XMFLOAT3 pos;
		XMFLOAT2 texCoord;
	};

	struct ConstantBuffer0_PS
	{
		XMFLOAT4 Color;
	};
	struct ConstantBuffer1_VS
	{
		XMFLOAT4X4 WorldViewProj;
	};

	const size_t ConstantBufferPerObjectAlignedSize = AlignUp<size_t>(sizeof(ConstantBuffer1_VS), 256);
	D3D12MA::Allocation* m_CbPerObjectUploadHeapAllocations[FRAME_BUFFER_COUNT];
	ComPtr<ID3D12Resource> m_CbPerObjectUploadHeaps[FRAME_BUFFER_COUNT];
	void* m_CbPerObjectAddress[FRAME_BUFFER_COUNT];
	uint32_t m_CubeIndexCount;

	ComPtr<ID3D12DescriptorHeap> m_MainDescriptorHeap[FRAME_BUFFER_COUNT];
	ComPtr<ID3D12Resource> m_ConstantBufferUploadHeap[FRAME_BUFFER_COUNT];
	D3D12MA::Allocation* m_ConstantBufferUploadAllocation[FRAME_BUFFER_COUNT];
	void* m_ConstantBufferAddress[FRAME_BUFFER_COUNT];

	ComPtr<ID3D12Resource> m_Texture;
	D3D12MA::Allocation* m_TextureAllocation;
};