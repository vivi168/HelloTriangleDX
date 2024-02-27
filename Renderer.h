#pragma once

#include "RendererHelper.h"
#include "D3D12MemAlloc.h"
#include "Mesh.h"
#include "Camera.h"

#include <list>
#include <unordered_map>

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

struct Geometry
{
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

struct Texture
{
	struct Header {
		uint16_t width;
		uint16_t height;
	} header;
	std::vector<uint8_t> pixels;
	std::string name;

	ComPtr<ID3D12Resource> m_Texture;
	D3D12MA::Allocation* m_TextureAllocation = nullptr;

	D3D12MA::Allocation* textureUploadAllocation = nullptr;

	static unsigned int texCount;
	unsigned int texIndex;

	void Read(std::string filename)
	{
		FILE* fp;
		fopen_s(&fp, filename.c_str(), "rb");
		assert(fp);

		name = filename;

		fread(&header, sizeof(Header), 1, fp);
		pixels.resize(ImageSize());
		fread(pixels.data(), sizeof(uint8_t), ImageSize(), fp);
	}

	void Unload()
	{
		m_Texture.Reset();
		m_TextureAllocation->Release();
		m_TextureAllocation = nullptr;
	}

	DXGI_FORMAT Format() const
	{
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	uint32_t BytesPerPixel() const
	{
		return 4;
	}

	uint32_t Width() const
	{
		return header.width;
	}

	uint32_t Height() const
	{
		return header.height;
	}

	uint64_t BytesPerRow() const
	{
		return Width() * BytesPerPixel();
	}

	uint64_t ImageSize() const
	{
		return Height() * BytesPerRow();
	}
};

class Renderer
{
public:
	Renderer(UINT width, UINT height, std::wstring name);

	UINT GetWidth() const { return m_width; }
	UINT GetHeight() const { return m_height; }
	const WCHAR* GetTitle() const { return m_title.c_str(); }

    void InitAdapter(DXGIUsage*, GPUSelection);
	void Init();
	void LoadAssets();
	void Update(float);
	void Render();
	void Cleanup();
	void PrintStatsString();

	void SetSceneCamera(Camera* cam)
	{
		m_Scene.camera = cam;
	}

	void AppendToScene(Model3D* model)
	{
		size_t cbIndex = ConstantBufferPerObjectAlignedSize * cbNextIndex++;

		m_Scene.nodes.push_back({model, cbIndex});
	}

	void ToggleRaster()
	{
		m_Raster = !m_Raster;
	}

private:
	void InitD3D();
	void InitFrameResources();

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

	static const UINT PRESENT_SYNC_INTERVAL;
	static const DXGI_FORMAT RENDER_TARGET_FORMAT;
	static const DXGI_FORMAT DEPTH_STENCIL_FORMAT;
	static const D3D_FEATURE_LEVEL D3D_FEATURE_LEVEL;

	static const bool ENABLE_DEBUG_LAYER;
	static const bool ENABLE_CPU_ALLOCATION_CALLBACKS;

	static const UINT NUM_DESCRIPTORS_PER_HEAP;

	UINT m_width;
	UINT m_height;
	float m_aspectRatio;

	std::wstring m_title;
	std::wstring m_assetsPath;

	// Pipeline objects
	DXGIUsage* m_DXGIUsage;
	ComPtr<IDXGIAdapter1> m_Adapter;

	ComPtr<ID3D12Device> m_Device;
	DXGI_ADAPTER_DESC1 m_AdapterDesc;
	ComPtr<D3D12MA::Allocator> m_Allocator;
	D3D12MA::ALLOCATION_CALLBACKS m_AllocationCallbacks; // Used only when ENABLE_CPU_ALLOCATION_CALLBACKS

	ComPtr<IDXGISwapChain3> m_SwapChain; // swapchain used to switch between render targets
	ComPtr<ID3D12CommandQueue> m_CommandQueue; // container for command lists
	ComPtr<ID3D12CommandAllocator> m_CommandAllocators[FRAME_BUFFER_COUNT]; // we want enough allocators for each buffer * number of threads (we only have one thread)
	ComPtr<ID3D12GraphicsCommandList> m_CommandList; // a command list we can record commands into, then execute them to render the frame
	ComPtr<ID3D12PipelineState> m_PipelineStateObject;

	// Synchronization objects.
	ComPtr<ID3D12Fence> m_Fences[FRAME_BUFFER_COUNT];    // an object that is locked while our command list is being executed by the gpu. We need as many
	//as we have allocators (more if we want to know when the gpu is finished with an asset)
	HANDLE m_FenceEvent; // a handle to an event when our m_Fences is unlocked by the gpu
	UINT64 m_FenceValues[FRAME_BUFFER_COUNT]; // this value is incremented each frame. each m_Fences will have its own value
	UINT m_FrameIndex; // current rtv we are on

	// Resources
	ComPtr<ID3D12DescriptorHeap> m_RtvDescriptorHeap; // a descriptor heap to hold resources like the render targets
	ComPtr<ID3D12Resource> m_RenderTargets[FRAME_BUFFER_COUNT]; // number of render targets equal to buffer count
	UINT m_RtvDescriptorSize; // size of the rtv descriptor on the m_Device (all front and back buffers will be the same size)

	ComPtr<ID3D12Resource> m_DepthStencilBuffer;
	D3D12MA::Allocation* m_DepthStencilAllocation;
	ComPtr<ID3D12DescriptorHeap> m_DepthStencilDescriptorHeap;

	ComPtr<ID3D12RootSignature> m_RootSignature;

	bool m_Raster = true;

	struct PerFrameCB0_ALL
	{
	    XMFLOAT4 Color;
		float time;
	};

	struct PerObjectCB1_VS
	{
	    XMFLOAT4X4 WorldViewProj;
	};

	const size_t ConstantBufferPerObjectAlignedSize = AlignUp<size_t>(sizeof(PerObjectCB1_VS), 256);
	D3D12MA::Allocation* m_CbPerObjectUploadHeapAllocations[FRAME_BUFFER_COUNT];
	ComPtr<ID3D12Resource> m_CbPerObjectUploadHeaps[FRAME_BUFFER_COUNT];
	void* m_CbPerObjectAddress[FRAME_BUFFER_COUNT];
	static unsigned int cbNextIndex;

	ComPtr<ID3D12DescriptorHeap> m_MainDescriptorHeap[FRAME_BUFFER_COUNT];
	ID3D12DescriptorHeap* m_pMainDescriptorHeap[FRAME_BUFFER_COUNT];
	ComPtr<ID3D12Resource> m_ConstantBufferUploadHeap[FRAME_BUFFER_COUNT];
	D3D12MA::Allocation* m_ConstantBufferUploadAllocation[FRAME_BUFFER_COUNT];
	void* m_ConstantBufferAddress[FRAME_BUFFER_COUNT];

	struct Scene
	{
		struct SceneNode
		{
			Model3D* model;
			size_t cbIndex;
		};

		std::list<SceneNode> nodes;
		Camera* camera;
	} m_Scene;

	std::unordered_map<std::string, std::unique_ptr<Geometry>> m_Geometries;
	std::unordered_map<std::string, std::unique_ptr<Texture>> m_Textures;

	void LoadMesh3D(Mesh3D*);
	Geometry* CreateGeometry(Mesh3D* mesh);
	Texture* CreateTexture(std::string name);
};
