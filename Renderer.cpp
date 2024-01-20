#include "stdafx.h"

#include "Win32Application.h"
#include "Renderer.h"
#include "Mesh.h"

#include <Shlwapi.h>
#include <atomic>

const UINT Renderer::PRESENT_SYNC_INTERVAL = 1;
const DXGI_FORMAT Renderer::RENDER_TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
const DXGI_FORMAT Renderer::DEPTH_STENCIL_FORMAT = DXGI_FORMAT_D32_FLOAT;
const D3D_FEATURE_LEVEL Renderer::D3D_FEATURE_LEVEL = D3D_FEATURE_LEVEL_12_0;

const bool Renderer::ENABLE_DEBUG_LAYER = true;
const bool Renderer::ENABLE_CPU_ALLOCATION_CALLBACKS = true;

// ===========

void DXGIUsage::Init()
{
	CoInitialize(NULL);

	CHECK_HR(CreateDXGIFactory1(IID_PPV_ARGS(&m_DXGIFactory)));
}

void DXGIUsage::PrintAdapterList() const
{
	UINT index = 0;
	ComPtr<IDXGIAdapter1> adapter;
	while (m_DXGIFactory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		const bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
		const wchar_t* const suffix = isSoftware ? L" (SOFTWARE)" : L"";
		wprintf(L"Adapter %u: %s%s\n", index, desc.Description, suffix);

		adapter.Reset();
		++index;
	}
}

ComPtr<IDXGIAdapter1> DXGIUsage::CreateAdapter(const GPUSelection& GPUSelection) const
{
	ComPtr<IDXGIAdapter1> adapter;

	if (GPUSelection.Index != UINT32_MAX)
	{
		// Cannot specify both index and name.
		if (!GPUSelection.Substring.empty())
		{
			return adapter;
		}

		CHECK_HR(m_DXGIFactory->EnumAdapters1(GPUSelection.Index, &adapter));
		return adapter;
	}

	if (!GPUSelection.Substring.empty())
	{
		ComPtr<IDXGIAdapter1> tmpAdapter;
		for (UINT i = 0; m_DXGIFactory->EnumAdapters1(i, &tmpAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
		{
			DXGI_ADAPTER_DESC1 desc;
			tmpAdapter->GetDesc1(&desc);
			if (StrStrI(desc.Description, GPUSelection.Substring.c_str()))
			{
				// Second matching adapter found - error.
				if (adapter)
				{
					adapter.Reset();
					return adapter;
				}
				// First matching adapter found.
				adapter = std::move(tmpAdapter);
			}
			else
			{
				tmpAdapter.Reset();
			}
		}
		// Found or not, return it.
		return adapter;
	}

	// Select first one.
	m_DXGIFactory->EnumAdapters1(0, &adapter);
	return adapter;
}

// ===========

static const bool ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT = true;
static void* const CUSTOM_ALLOCATION_PRIVATE_DATA = (void*)(uintptr_t)0xDEADC0DE;

static std::atomic<size_t> g_CpuAllocationCount{ 0 };

static void* CustomAllocate(size_t Size, size_t Alignment, void* pPrivateData)
{
	assert(pPrivateData == CUSTOM_ALLOCATION_PRIVATE_DATA);
	void* memory = _aligned_malloc(Size, Alignment);
	if (ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT)
	{
		wprintf(L"Allocate Size=%llu Alignment=%llu -> %p\n", Size, Alignment, memory);
	}
	++g_CpuAllocationCount;
	return memory;
}

static void CustomFree(void* pMemory, void* pPrivateData)
{
	assert(pPrivateData == CUSTOM_ALLOCATION_PRIVATE_DATA);
	if (pMemory)
	{
		--g_CpuAllocationCount;
		if (ENABLE_CPU_ALLOCATION_CALLBACKS_PRINT)
		{
			wprintf(L"Free %p\n", pMemory);
		}
		_aligned_free(pMemory);
	}
}

// ===========

Renderer::Renderer(UINT width, UINT height, std::wstring name)
	: m_width(width)
	, m_height(height)
	, m_title(name)
	, m_DXGIUsage(nullptr)
{
	WCHAR assetsPath[512];
	GetAssetsPath(assetsPath, _countof(assetsPath));
	m_assetsPath = assetsPath;

	m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
}

void Renderer::InitAdapter(DXGIUsage* dxgiUsage, GPUSelection gpuSelection)
{
	m_DXGIUsage = dxgiUsage;
	assert(m_DXGIUsage);

	m_Adapter = m_DXGIUsage->CreateAdapter(gpuSelection);
	CHECK_BOOL(m_Adapter);

	CHECK_HR(m_Adapter->GetDesc1(&m_AdapterDesc));
}

void Renderer::InitD3D()
{
	// Must be done before D3D12 device is created.
	if (ENABLE_DEBUG_LAYER)
	{
		ComPtr<ID3D12Debug> debug;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
			debug->EnableDebugLayer();
	}

	// Create the m_Device
	ID3D12Device* device = nullptr;
	CHECK_HR(D3D12CreateDevice(
		m_Adapter.Get(),
		D3D_FEATURE_LEVEL,
		IID_PPV_ARGS(&device)));
	m_Device.Attach(device);

	// Create allocator

	{
		D3D12MA::ALLOCATOR_DESC desc = {};
		desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;
		desc.pDevice = device;
		desc.pAdapter = m_Adapter.Get();

		if (ENABLE_CPU_ALLOCATION_CALLBACKS)
		{
			m_AllocationCallbacks.pAllocate = &CustomAllocate;
			m_AllocationCallbacks.pFree = &CustomFree;
			m_AllocationCallbacks.pPrivateData = CUSTOM_ALLOCATION_PRIVATE_DATA;
			desc.pAllocationCallbacks = &m_AllocationCallbacks;
		}

		CHECK_HR(D3D12MA::CreateAllocator(&desc, &m_Allocator));
	}

	PrintAdapterInformation(m_Adapter.Get());
	wprintf(L"\n");

	// -- Create the Command Queue -- //

	D3D12_COMMAND_QUEUE_DESC cqDesc = {}; // we will be using all the default values

	ID3D12CommandQueue* commandQueue = nullptr;
	CHECK_HR(m_Device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue))); // create the command queue
	m_CommandQueue.Attach(commandQueue);

	// -- Create the Swap Chain (double/tripple buffering) -- //

	DXGI_MODE_DESC backBufferDesc = {}; // this is to describe our display mode
	backBufferDesc.Width = m_width; // buffer width
	backBufferDesc.Height = m_height; // buffer height
	backBufferDesc.Format = RENDER_TARGET_FORMAT; // format of the buffer (rgba 32 bits, 8 bits for each chanel)

	// describe our multi-sampling. We are not multi-sampling, so we set the count to 1 (we need at least one sample of course)
	DXGI_SAMPLE_DESC sampleDesc = {};
	sampleDesc.Count = 1; // multisample count (no multisampling, so we just put 1, since we still need 1 sample)

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = FRAME_BUFFER_COUNT; // number of buffers we have
	swapChainDesc.BufferDesc = backBufferDesc; // our back buffer description
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // this says the pipeline will render to this swap chain
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // dxgi will discard the buffer (data) after we call present
	swapChainDesc.OutputWindow = Win32Application::GetHwnd(); // handle to our window
	swapChainDesc.SampleDesc = sampleDesc; // our multi-sampling description
	swapChainDesc.Windowed = true; // set to true, then if in fullscreen must call SetFullScreenState with true for full screen to get uncapped fps

	IDXGISwapChain* tempSwapChain;

	CHECK_HR(m_DXGIUsage->GetDXGIFactory()->CreateSwapChain(
		m_CommandQueue.Get(), // the queue will be flushed once the swap chain is created
		&swapChainDesc, // give it the swap chain description we created above
		&tempSwapChain // store the created swap chain in a temp IDXGISwapChain interface
	));

	m_SwapChain.Attach(static_cast<IDXGISwapChain3*>(tempSwapChain));

	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

	// -- Create the Back Buffers (render target views) Descriptor Heap -- //

	// describe an rtv descriptor heap and create
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = FRAME_BUFFER_COUNT; // number of descriptors for this heap.
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; // this heap is a render target view heap

	// This heap will not be directly referenced by the shaders (not shader visible), as this will store the output from the pipeline
	// otherwise we would set the heap's flag to D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ID3D12DescriptorHeap* rtvDescriptorHeap = nullptr;
	CHECK_HR(m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));
	m_RtvDescriptorHeap.Attach(rtvDescriptorHeap);

	// get the size of a descriptor in this heap (this is a rtv heap, so only rtv descriptors should be stored in it.
	// descriptor sizes may vary from m_Device to m_Device, which is why there is no set size and we must ask the
	// m_Device to give us the size. we will use this size to increment a descriptor handle offset
	m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// get a handle to the first descriptor in the descriptor heap. a handle is basically a pointer,
	// but we cannot literally use it like a c++ pointer.
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{ m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };

	// Create a RTV for each buffer (double buffering is two buffers, tripple buffering is 3).
	for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
	{
		// first we get the n'th buffer in the swap chain and store it in the n'th
		// position of our ID3D12Resource array
		ID3D12Resource* res = nullptr;
		CHECK_HR(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&res)));
		m_RenderTargets[i].Attach(res);

		// the we "create" a render target view which binds the swap chain buffer (ID3D12Resource[n]) to the rtv handle
		m_Device->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, rtvHandle);

		// we increment the rtv handle by the rtv descriptor size we got above
		rtvHandle.ptr += m_RtvDescriptorSize;
	}

	// -- Create the Command Allocators -- //

	for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
	{
		ID3D12CommandAllocator* commandAllocator = nullptr;
		CHECK_HR(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
		m_CommandAllocators[i].Attach(commandAllocator);
	}

	// create the command list with the first allocator
	CHECK_HR(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_CommandAllocators[0].Get(), NULL, IID_PPV_ARGS(&m_CommandList)));

	// command lists are created in the recording state. our main loop will set it up for recording again so close it now
	m_CommandList->Close();

	// create a depth stencil descriptor heap so we can get a pointer to the depth stencil buffer
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	CHECK_HR(m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DepthStencilDescriptorHeap)));

	D3D12_CLEAR_VALUE depthOptimizedClearValue = {};
	depthOptimizedClearValue.Format = DEPTH_STENCIL_FORMAT;
	depthOptimizedClearValue.DepthStencil.Depth = 1.0f;
	depthOptimizedClearValue.DepthStencil.Stencil = 0;

	D3D12MA::ALLOCATION_DESC depthStencilAllocDesc = {};
	depthStencilAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC depthStencilResourceDesc = {};
	depthStencilResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilResourceDesc.Alignment = 0;
	depthStencilResourceDesc.Width = m_width;
	depthStencilResourceDesc.Height = m_height;
	depthStencilResourceDesc.DepthOrArraySize = 1;
	depthStencilResourceDesc.MipLevels = 1;
	depthStencilResourceDesc.Format = DEPTH_STENCIL_FORMAT;
	depthStencilResourceDesc.SampleDesc.Count = 1;
	depthStencilResourceDesc.SampleDesc.Quality = 0;
	depthStencilResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	CHECK_HR(m_Allocator->CreateResource(
		&depthStencilAllocDesc,
		&depthStencilResourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthOptimizedClearValue,
		&m_DepthStencilAllocation,
		IID_PPV_ARGS(&m_DepthStencilBuffer)
	));
	CHECK_HR(m_DepthStencilBuffer->SetName(L"Depth/Stencil Resource Heap"));
	m_DepthStencilAllocation->SetName(L"Depth/Stencil Resource Heap");

	D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc = {};
	depthStencilDesc.Format = DEPTH_STENCIL_FORMAT;
	depthStencilDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	depthStencilDesc.Flags = D3D12_DSV_FLAG_NONE;
	m_Device->CreateDepthStencilView(m_DepthStencilBuffer.Get(), &depthStencilDesc, m_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// -- Create a Fence & Fence Event -- //

	// create the fences
	for (int i = 0; i < FRAME_BUFFER_COUNT; i++)
	{
		ID3D12Fence* fence = nullptr;
		CHECK_HR(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
		m_Fences[i].Attach(fence);
		m_FenceValues[i] = 0; // set the initial m_Fences value to 0
	}

	// create a handle to a m_Fences event
	m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(m_FenceEvent);

	D3D12_DESCRIPTOR_RANGE cbDescriptorRange;
	cbDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
	cbDescriptorRange.NumDescriptors = 1;
	cbDescriptorRange.BaseShaderRegister = 0;
	cbDescriptorRange.RegisterSpace = 0;
	cbDescriptorRange.OffsetInDescriptorsFromTableStart = 0;

	D3D12_DESCRIPTOR_RANGE textureDescRange;
	textureDescRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	textureDescRange.NumDescriptors = 1;
	textureDescRange.BaseShaderRegister = 0;
	textureDescRange.RegisterSpace = 0;
	textureDescRange.OffsetInDescriptorsFromTableStart = 1;

	D3D12_ROOT_PARAMETER  rootParameters[3];

	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].DescriptorTable = { 1, &cbDescriptorRange };
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].Descriptor = { 1, 0 };
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].DescriptorTable = { 1, &textureDescRange };
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// create root signature

	// create a static sampler
	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumStaticSamplers = 1;
	rootSignatureDesc.pStaticSamplers = &sampler;
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

	ComPtr<ID3DBlob> signatureBlob;
	ID3DBlob* signatureBlobPtr;
	CHECK_HR(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlobPtr, nullptr));
	signatureBlob.Attach(signatureBlobPtr);

	ID3D12RootSignature* rootSignature = nullptr;
	CHECK_HR(device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));
	m_RootSignature.Attach(rootSignature);

	for (int i = 0; i < FRAME_BUFFER_COUNT; ++i)
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 2;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		CHECK_HR(m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_MainDescriptorHeap[i])));
	}

	// # CONSTANT BUFFER

	for (int i = 0; i < FRAME_BUFFER_COUNT; ++i)
	{
		D3D12MA::ALLOCATION_DESC constantBufferUploadAllocDesc = {};
		constantBufferUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC constantBufferResourceDesc = {};
		constantBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		constantBufferResourceDesc.Alignment = 0;
		constantBufferResourceDesc.Width = 1024 * 64;
		constantBufferResourceDesc.Height = 1;
		constantBufferResourceDesc.DepthOrArraySize = 1;
		constantBufferResourceDesc.MipLevels = 1;
		constantBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		constantBufferResourceDesc.SampleDesc.Count = 1;
		constantBufferResourceDesc.SampleDesc.Quality = 0;
		constantBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		constantBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
		CHECK_HR(m_Allocator->CreateResource(
			&constantBufferUploadAllocDesc,
			&constantBufferResourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			&m_ConstantBufferUploadAllocation[i],
			IID_PPV_ARGS(&m_ConstantBufferUploadHeap[i])));
		m_ConstantBufferUploadHeap[i]->SetName(L"Constant Buffer Upload Resource Heap");
		m_ConstantBufferUploadAllocation[i]->SetName(L"Constant Buffer Upload Resource Heap");

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = m_ConstantBufferUploadHeap[i]->GetGPUVirtualAddress();
		cbvDesc.SizeInBytes = AlignUp<UINT>(sizeof(ConstantBuffer0_PS), 256);
		m_Device->CreateConstantBufferView(&cbvDesc, m_MainDescriptorHeap[i]->GetCPUDescriptorHandleForHeapStart());

		CHECK_HR(m_ConstantBufferUploadHeap[i]->Map(0, &EMPTY_RANGE, &m_ConstantBufferAddress[i]));
	}

	// create input layout

	// The input layout is used by the Input Assembler so that it knows
	// how to read the vertex data bound to it.

	const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	// create a pipeline state object (PSO)

	// In a real application, you will have many pso's. for each different shader
	// or different combinations of shaders, different blend states or different rasterizer states,
	// different topology types (point, line, triangle, patch), or a different number
	// of render targets you will need a pso

	// VS is the only required shader for a pso. You might be wondering when a case would be where
	// you only set the VS. It's possible that you have a pso that only outputs data with the stream
	// output, and not on a render target, which means you would not need anything after the stream
	// output.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {}; // a structure to define a pso
	psoDesc.InputLayout.NumElements = _countof(inputLayout);
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.pRootSignature = m_RootSignature.Get(); // the root signature that describes the input data this pso needs

	auto vertexShaderBlob = ReadData(GetAssetFullPath(L"VS.cso").c_str());
	auto pixelShaderBlob = ReadData(GetAssetFullPath(L"PS.cso").c_str());

	D3D12_SHADER_BYTECODE vertexShader = { vertexShaderBlob.data(), vertexShaderBlob.size() };
	D3D12_SHADER_BYTECODE pixelShader = { pixelShaderBlob.data(), pixelShaderBlob.size() };

	psoDesc.VS = vertexShader;
	psoDesc.PS = pixelShader;

	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // type of topology we are drawing
	psoDesc.RTVFormats[0] = RENDER_TARGET_FORMAT; // format of the render target
	psoDesc.DSVFormat = DEPTH_STENCIL_FORMAT;
	psoDesc.SampleDesc = sampleDesc; // must be the same sample description as the swapchain and depth/stencil buffer
	psoDesc.SampleMask = 0xffffffff; // sample mask has to do with multi-sampling. 0xffffffff means point sampling is done
	SetDefaultRasterizerDesc(psoDesc.RasterizerState);
	SetDefaultBlendDesc(psoDesc.BlendState);
	psoDesc.NumRenderTargets = 1; // we are only binding one render target
	SetDefaultDepthStencilDesc(psoDesc.DepthStencilState);

	// create the pso
	ID3D12PipelineState* pipelineStateObject;
	CHECK_HR(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineStateObject)));
	m_PipelineStateObject.Attach(pipelineStateObject);

	// Create vertex buffer
	Mesh3D cube;
	cube.Read("assets/cube.objb");

	const UINT64 vBufferSize = cube.VertexBufferSize();

	// create default heap
	// default heap is memory on the GPU. Only the GPU has access to this memory
	// To get data into this heap, we will have to upload the data using
	// an upload heap
	D3D12MA::ALLOCATION_DESC vertexBufferAllocDesc = {};
	vertexBufferAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC vertexBufferResourceDesc = {};
	vertexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexBufferResourceDesc.Alignment = 0;
	vertexBufferResourceDesc.Width = vBufferSize;
	vertexBufferResourceDesc.Height = 1;
	vertexBufferResourceDesc.DepthOrArraySize = 1;
	vertexBufferResourceDesc.MipLevels = 1;
	vertexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	vertexBufferResourceDesc.SampleDesc.Count = 1;
	vertexBufferResourceDesc.SampleDesc.Quality = 0;
	vertexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	vertexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ID3D12Resource* vertexBufferPtr;
	CHECK_HR(m_Allocator->CreateResource(
		&vertexBufferAllocDesc,
		&vertexBufferResourceDesc, // resource description for a buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // we will start this heap in the copy destination state since we will copy data
		// from the upload heap to this heap
		nullptr, // optimized clear value must be null for this type of resource. used for render targets and depth/stencil buffers
		&m_VertexBufferAllocation,
		IID_PPV_ARGS(&vertexBufferPtr)));
	m_VertexBuffer.Attach(vertexBufferPtr);

	// we can give resource heaps a name so when we debug with the graphics debugger we know what resource we are looking at
	m_VertexBuffer->SetName(L"Vertex Buffer Resource Heap");
	m_VertexBufferAllocation->SetName(L"Vertex Buffer Resource Heap");

	// create upload heap
	// upload heaps are used to upload data to the GPU. CPU can write to it, GPU can read from it
	// We will upload the vertex buffer using this heap to the default heap
	D3D12MA::ALLOCATION_DESC vBufferUploadAllocDesc = {};
	vBufferUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC vertexBufferUploadResourceDesc = {};
	vertexBufferUploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexBufferUploadResourceDesc.Alignment = 0;
	vertexBufferUploadResourceDesc.Width = vBufferSize;
	vertexBufferUploadResourceDesc.Height = 1;
	vertexBufferUploadResourceDesc.DepthOrArraySize = 1;
	vertexBufferUploadResourceDesc.MipLevels = 1;
	vertexBufferUploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	vertexBufferUploadResourceDesc.SampleDesc.Count = 1;
	vertexBufferUploadResourceDesc.SampleDesc.Quality = 0;
	vertexBufferUploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	vertexBufferUploadResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ComPtr<ID3D12Resource> vBufferUploadHeap;
	D3D12MA::Allocation* vBufferUploadHeapAllocation = nullptr;
	CHECK_HR(m_Allocator->CreateResource(
		&vBufferUploadAllocDesc,
		&vertexBufferUploadResourceDesc, // resource description for a buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // GPU will read from this buffer and copy its contents to the default heap
		nullptr,
		&vBufferUploadHeapAllocation,
		IID_PPV_ARGS(&vBufferUploadHeap)));
	vBufferUploadHeap->SetName(L"Vertex Buffer Upload Resource Heap");
	vBufferUploadHeapAllocation->SetName(L"Vertex Buffer Upload Resource Heap");

	// store vertex buffer in upload heap
	D3D12_SUBRESOURCE_DATA vertexData = {};
	vertexData.pData = reinterpret_cast<BYTE*>(cube.vertices.data()); // pointer to our vertex array
	vertexData.RowPitch = vBufferSize; // size of all our triangle vertex data
	vertexData.SlicePitch = vBufferSize; // also the size of our triangle vertex data

	CHECK_HR(m_CommandList->Reset(m_CommandAllocators[m_FrameIndex].Get(), NULL));

	// we are now creating a command with the command list to copy the data from
	// the upload heap to the default heap
	UINT64 r = UpdateSubresources(m_CommandList.Get(), m_VertexBuffer.Get(), vBufferUploadHeap.Get(), 0, 0, 1, &vertexData);
	assert(r);

	// transition the vertex buffer data from copy destination state to vertex buffer state
	D3D12_RESOURCE_BARRIER vbBarrier = {};
	vbBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	vbBarrier.Transition.pResource = m_VertexBuffer.Get();
	vbBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	vbBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	vbBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_CommandList->ResourceBarrier(1, &vbBarrier);

	// Create index buffer
	m_CubeIndexCount = cube.header.numIndices;
	UINT64 iBufferSize = cube.IndexBufferSize();

	// create default heap to hold index buffer
	D3D12MA::ALLOCATION_DESC indexBufferAllocDesc = {};
	indexBufferAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC indexBufferResourceDesc = {};
	indexBufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	indexBufferResourceDesc.Alignment = 0;
	indexBufferResourceDesc.Width = iBufferSize;
	indexBufferResourceDesc.Height = 1;
	indexBufferResourceDesc.DepthOrArraySize = 1;
	indexBufferResourceDesc.MipLevels = 1;
	indexBufferResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	indexBufferResourceDesc.SampleDesc.Count = 1;
	indexBufferResourceDesc.SampleDesc.Quality = 0;
	indexBufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	indexBufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	CHECK_HR(m_Allocator->CreateResource(
		&indexBufferAllocDesc,
		&indexBufferResourceDesc, // resource description for a buffer
		D3D12_RESOURCE_STATE_COPY_DEST, // start in the copy destination state
		nullptr, // optimized clear value must be null for this type of resource
		&m_IndexBufferAllocation,
		IID_PPV_ARGS(&m_IndexBuffer)));

	// we can give resource heaps a name so when we debug with the graphics debugger we know what resource we are looking at
	m_IndexBuffer->SetName(L"Index Buffer Resource Heap");
	m_IndexBufferAllocation->SetName(L"Index Buffer Resource Heap");

	// create upload heap to upload index buffer
	D3D12MA::ALLOCATION_DESC iBufferUploadAllocDesc = {};
	iBufferUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC indexBufferUploadResourceDesc = {};
	indexBufferUploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	indexBufferUploadResourceDesc.Alignment = 0;
	indexBufferUploadResourceDesc.Width = iBufferSize;
	indexBufferUploadResourceDesc.Height = 1;
	indexBufferUploadResourceDesc.DepthOrArraySize = 1;
	indexBufferUploadResourceDesc.MipLevels = 1;
	indexBufferUploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	indexBufferUploadResourceDesc.SampleDesc.Count = 1;
	indexBufferUploadResourceDesc.SampleDesc.Quality = 0;
	indexBufferUploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	indexBufferUploadResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ComPtr<ID3D12Resource> iBufferUploadHeap;
	D3D12MA::Allocation* iBufferUploadHeapAllocation = nullptr;
	CHECK_HR(m_Allocator->CreateResource(
		&iBufferUploadAllocDesc,
		&indexBufferUploadResourceDesc, // resource description for a buffer
		D3D12_RESOURCE_STATE_GENERIC_READ, // GPU will read from this buffer and copy its contents to the default heap
		nullptr,
		&iBufferUploadHeapAllocation,
		IID_PPV_ARGS(&iBufferUploadHeap)));
	CHECK_HR(iBufferUploadHeap->SetName(L"Index Buffer Upload Resource Heap"));
	iBufferUploadHeapAllocation->SetName(L"Index Buffer Upload Resource Heap");

	// store vertex buffer in upload heap
	D3D12_SUBRESOURCE_DATA indexData = {};
	indexData.pData = reinterpret_cast<BYTE*>(cube.indices.data()); // pointer to our index array
	indexData.RowPitch = iBufferSize; // size of all our index buffer
	indexData.SlicePitch = iBufferSize; // also the size of our index buffer

	// we are now creating a command with the command list to copy the data from
	// the upload heap to the default heap
	r = UpdateSubresources(m_CommandList.Get(), m_IndexBuffer.Get(), iBufferUploadHeap.Get(), 0, 0, 1, &indexData);
	assert(r);

	// transition the index buffer data from copy destination state to vertex buffer state
	D3D12_RESOURCE_BARRIER ibBarrier = {};
	ibBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	ibBarrier.Transition.pResource = m_IndexBuffer.Get();
	ibBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	ibBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
	ibBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_CommandList->ResourceBarrier(1, &ibBarrier);

	// create a vertex buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVirtualAddress() method
	m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
	m_VertexBufferView.StrideInBytes = sizeof(Vertex);
	m_VertexBufferView.SizeInBytes = (UINT)vBufferSize;

	// create a index buffer view for the triangle. We get the GPU memory address to the vertex pointer using the GetGPUVirtualAddress() method
	m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
	m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
	m_IndexBufferView.SizeInBytes = (UINT)iBufferSize;

	D3D12MA::ALLOCATION_DESC cbPerObjectUploadAllocDesc = {};
	cbPerObjectUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC cbPerObjectUploadResourceDesc = {};
	cbPerObjectUploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbPerObjectUploadResourceDesc.Alignment = 0;
	cbPerObjectUploadResourceDesc.Width = 1024 * 64;
	cbPerObjectUploadResourceDesc.Height = 1;
	cbPerObjectUploadResourceDesc.DepthOrArraySize = 1;
	cbPerObjectUploadResourceDesc.MipLevels = 1;
	cbPerObjectUploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	cbPerObjectUploadResourceDesc.SampleDesc.Count = 1;
	cbPerObjectUploadResourceDesc.SampleDesc.Quality = 0;
	cbPerObjectUploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	cbPerObjectUploadResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	for (size_t i = 0; i < FRAME_BUFFER_COUNT; ++i)
	{
		// create resource for cube 1
		CHECK_HR(m_Allocator->CreateResource(
			&cbPerObjectUploadAllocDesc,
			&cbPerObjectUploadResourceDesc, // size of the resource heap. Must be a multiple of 64KB for single-textures and constant buffers
			D3D12_RESOURCE_STATE_GENERIC_READ, // will be data that is read from so we keep it in the generic read state
			nullptr, // we do not have use an optimized clear value for constant buffers
			&m_CbPerObjectUploadHeapAllocations[i],
			IID_PPV_ARGS(&m_CbPerObjectUploadHeaps[i])));
		m_CbPerObjectUploadHeaps[i]->SetName(L"Constant Buffer Upload Resource Heap");
		m_CbPerObjectUploadHeapAllocations[i]->SetName(L"Constant Buffer Upload Resource Heap");

		CHECK_HR(m_CbPerObjectUploadHeaps[i]->Map(0, &EMPTY_RANGE, &m_CbPerObjectAddress[i]));
	}

	// # TEXTURE

	Texture tex;
	tex.Read("assets/cube.raw");

	D3D12_RESOURCE_DESC textureDesc;
	textureDesc = {};
	textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	textureDesc.Alignment = 0;
	textureDesc.Width = tex.Width();
	textureDesc.Height = tex.Height();
	textureDesc.DepthOrArraySize = 1;
	textureDesc.MipLevels = 1;
	textureDesc.Format = tex.Format();
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12MA::ALLOCATION_DESC textureAllocDesc = {};
	textureAllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
	CHECK_HR(m_Allocator->CreateResource(
		&textureAllocDesc,
		&textureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr, // pOptimizedClearValue
		&m_TextureAllocation,
		IID_PPV_ARGS(&m_Texture)));
	m_Texture->SetName(L"texture");
	m_TextureAllocation->SetName(L"texture");

	UINT64 textureUploadBufferSize;
	device->GetCopyableFootprints(
		&textureDesc,
		0, // FirstSubresource
		1, // NumSubresources
		0, // BaseOffset
		nullptr, // pLayouts
		nullptr, // pNumRows
		nullptr, // pRowSizeInBytes
		&textureUploadBufferSize); // pTotalBytes

	D3D12MA::ALLOCATION_DESC textureUploadAllocDesc = {};
	textureUploadAllocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC textureUploadResourceDesc = {};
	textureUploadResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	textureUploadResourceDesc.Alignment = 0;
	textureUploadResourceDesc.Width = textureUploadBufferSize;
	textureUploadResourceDesc.Height = 1;
	textureUploadResourceDesc.DepthOrArraySize = 1;
	textureUploadResourceDesc.MipLevels = 1;
	textureUploadResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	textureUploadResourceDesc.SampleDesc.Count = 1;
	textureUploadResourceDesc.SampleDesc.Quality = 0;
	textureUploadResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	textureUploadResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ComPtr<ID3D12Resource> textureUpload;
	D3D12MA::Allocation* textureUploadAllocation;
	CHECK_HR(m_Allocator->CreateResource(
		&textureUploadAllocDesc,
		&textureUploadResourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, // pOptimizedClearValue
		&textureUploadAllocation,
		IID_PPV_ARGS(&textureUpload)));
	textureUpload->SetName(L"textureUpload");
	textureUploadAllocation->SetName(L"textureUpload");

	D3D12_SUBRESOURCE_DATA textureSubresourceData = {};
	textureSubresourceData.pData = tex.pixels.data();
	textureSubresourceData.RowPitch = tex.BytesPerRow();
	textureSubresourceData.SlicePitch = tex.ImageSize();

	UpdateSubresources(m_CommandList.Get(), m_Texture.Get(), textureUpload.Get(), 0, 0, 1, &textureSubresourceData);

	D3D12_RESOURCE_BARRIER textureBarrier = {};
	textureBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	textureBarrier.Transition.pResource = m_Texture.Get();
	textureBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	textureBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	textureBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_CommandList->ResourceBarrier(1, &textureBarrier);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = textureDesc.Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	for (size_t i = 0; i < FRAME_BUFFER_COUNT; ++i)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE descHandle = {
			m_MainDescriptorHeap[i]->GetCPUDescriptorHandleForHeapStart().ptr +
			m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) };
		m_Device->CreateShaderResourceView(m_Texture.Get(), &srvDesc, descHandle);
	}

	// # END OF INITIAL COMMAND LIST

	// Now we execute the command list to upload the initial assets (triangle data)
	m_CommandList->Close();
	ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// increment the fence value now, otherwise the buffer might not be uploaded by the time we start drawing
	WaitGPUIdle(m_FrameIndex);

	textureUploadAllocation->Release();
	iBufferUploadHeapAllocation->Release();
	vBufferUploadHeapAllocation->Release();
}

void Renderer::Update(float time)
{
	{
		const float r = sin(0.5f * time * (XM_PI * 2.f)) * 0.5f + 0.5f;
		ConstantBuffer0_PS cb;
		cb.Color = XMFLOAT4(r, 1.f, 1.f, 1.f);
		memcpy(m_ConstantBufferAddress[m_FrameIndex], &cb, sizeof(cb));
	}

	{
		const XMMATRIX projection = XMMatrixPerspectiveFovLH(
			45.f * (XM_PI / 180.f),
			m_aspectRatio,
			0.1f,
			1000.f);

		XMVECTOR at = XMVectorSet(0.f, 0.f, 0.f, 1.f);
		XMVECTOR eye = XMVectorSet(0.f, 3.5f, -5.5f, 1.f);
		XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 1.f);
		XMMATRIX view = XMMatrixLookAtLH(eye, at, up);

		XMMATRIX viewProjection = XMMatrixMultiply(view, projection);

		// cube 1

		XMMATRIX cube1World = XMMatrixRotationY(time);

		ConstantBuffer1_VS cb;
		cb.time = time;

		XMMATRIX worldViewProjection = XMMatrixMultiplyTranspose(cube1World, viewProjection);
		XMStoreFloat4x4(&cb.WorldViewProj, worldViewProjection);
		memcpy(m_CbPerObjectAddress[m_FrameIndex], &cb, sizeof(cb));

		// cube 2
		XMMATRIX scale = XMMatrixScaling(0.5f, 0.5f, 0.5f);
		XMMATRIX rot = XMMatrixRotationX(time * 2.0f);
		XMMATRIX trans = XMMatrixTranslation(-2.f, 0.f, 0.f);

		XMMATRIX scale_rot = XMMatrixMultiply(scale, rot);
		XMMATRIX scale_rot_trans = XMMatrixMultiply(scale_rot, trans);
		XMMATRIX cube2World = XMMatrixMultiply(scale_rot_trans, cube1World);

		worldViewProjection = XMMatrixMultiplyTranspose(cube2World, viewProjection);
		XMStoreFloat4x4(&cb.WorldViewProj, worldViewProjection);
		// TODO: helper function to get address of object buffer in cbBuffer
		memcpy((char*)m_CbPerObjectAddress[m_FrameIndex] + ConstantBufferPerObjectAlignedSize, &cb, sizeof(cb));
	}
}

void Renderer::Render()
{
	// # Here was UpdatePipeline function.

	// swap the current rtv buffer index so we draw on the correct buffer
	m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
	// We have to wait for the gpu to finish with the command allocator before we reset it
	WaitForFrame(m_FrameIndex);
	// increment m_FenceValues for next frame
	m_FenceValues[m_FrameIndex]++;

	// we can only reset an allocator once the gpu is done with it
	// resetting an allocator frees the memory that the command list was stored in
	CHECK_HR(m_CommandAllocators[m_FrameIndex]->Reset());

	// reset the command list. by resetting the command list we are putting it into
	// a recording state so we can start recording commands into the command allocator.
	// the command allocator that we reference here may have multiple command lists
	// associated with it, but only one can be recording at any time. Make sure
	// that any other command lists associated to this command allocator are in
	// the closed state (not recording).
	// Here you will pass an initial pipeline state object as the second parameter,
	// but in this tutorial we are only clearing the rtv, and do not actually need
	// anything but an initial default pipeline, which is what we get by setting
	// the second parameter to NULL
	CHECK_HR(m_CommandList->Reset(m_CommandAllocators[m_FrameIndex].Get(), NULL));

	// here we start recording commands into the m_CommandList (which all the commands will be stored in the m_CommandAllocators)

	// transition the "m_FrameIndex" render target from the present state to the render target state so the command list draws to it starting from here
	D3D12_RESOURCE_BARRIER presentToRenderTargetBarrier = {};
	presentToRenderTargetBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	presentToRenderTargetBarrier.Transition.pResource = m_RenderTargets[m_FrameIndex].Get();
	presentToRenderTargetBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	presentToRenderTargetBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	presentToRenderTargetBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_CommandList->ResourceBarrier(1, &presentToRenderTargetBarrier);

	// here we again get the handle to our current render target view so we can set it as the render target in the output merger stage of the pipeline
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = {
		m_RtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr + m_FrameIndex * m_RtvDescriptorSize };
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		m_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// set the render target for the output merger stage (the output of the pipeline)
	m_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	m_CommandList->ClearDepthStencilView(m_DepthStencilDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// Clear the render target by using the ClearRenderTargetView command
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	m_CommandList->SetPipelineState(m_PipelineStateObject.Get());

	m_CommandList->SetGraphicsRootSignature(m_RootSignature.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { m_MainDescriptorHeap[m_FrameIndex].Get() };
	m_CommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	m_CommandList->SetGraphicsRootDescriptorTable(0, m_MainDescriptorHeap[m_FrameIndex]->GetGPUDescriptorHandleForHeapStart());
	m_CommandList->SetGraphicsRootDescriptorTable(2, m_MainDescriptorHeap[m_FrameIndex]->GetGPUDescriptorHandleForHeapStart());

	D3D12_VIEWPORT viewport{ 0.f, 0.f, (float)m_width, (float)m_height, 0.f, 1.f };
	m_CommandList->RSSetViewports(1, &viewport); // set the viewports

	D3D12_RECT scissorRect{ 0, 0, m_width, m_height };
	m_CommandList->RSSetScissorRects(1, &scissorRect); // set the scissor rects

	m_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // set the primitive topology
	m_CommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView); // set the vertex buffer (using the vertex buffer view)
	m_CommandList->IASetIndexBuffer(&m_IndexBufferView);

	m_CommandList->SetGraphicsRootConstantBufferView(1,
		m_CbPerObjectUploadHeaps[m_FrameIndex]->GetGPUVirtualAddress());
	m_CommandList->DrawIndexedInstanced(m_CubeIndexCount, 1, 0, 0, 0);

	m_CommandList->SetGraphicsRootConstantBufferView(1,
		m_CbPerObjectUploadHeaps[m_FrameIndex]->GetGPUVirtualAddress() + ConstantBufferPerObjectAlignedSize);
	m_CommandList->DrawIndexedInstanced(m_CubeIndexCount, 1, 0, 0, 0);

	// transition the "m_FrameIndex" render target from the render target state to the present state. If the debug layer is enabled, you will receive a
	// warning if present is called on the render target when it's not in the present state
	D3D12_RESOURCE_BARRIER renderTargetToPresentBarrier = {};
	renderTargetToPresentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	renderTargetToPresentBarrier.Transition.pResource = m_RenderTargets[m_FrameIndex].Get();
	renderTargetToPresentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	renderTargetToPresentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	renderTargetToPresentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_CommandList->ResourceBarrier(1, &renderTargetToPresentBarrier);

	CHECK_HR(m_CommandList->Close());

	// ================

	// create an array of command lists (only one command list here)
	ID3D12CommandList* ppCommandLists[] = { m_CommandList.Get() };

	// execute the array of command lists
	m_CommandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// this command goes in at the end of our command queue. we will know when our command queue
	// has finished because the m_Fences value will be set to "m_FenceValues" from the GPU since the command
	// queue is being executed on the GPU
	CHECK_HR(m_CommandQueue->Signal(m_Fences[m_FrameIndex].Get(), m_FenceValues[m_FrameIndex]));

	// present the current backbuffer
	CHECK_HR(m_SwapChain->Present(PRESENT_SYNC_INTERVAL, 0));
}

void Renderer::Cleanup()
{
	// wait for the gpu to finish all frames
	for (size_t i = 0; i < FRAME_BUFFER_COUNT; ++i)
	{
		WaitForFrame(i);
		CHECK_HR(m_CommandQueue->Wait(m_Fences[i].Get(), m_FenceValues[i]));
	}

	// get swapchain out of full screen before exiting
	BOOL fs = false;
	CHECK_HR(m_SwapChain->GetFullscreenState(&fs, NULL));
	if (fs)
		m_SwapChain->SetFullscreenState(false, NULL);

	WaitGPUIdle(0);

	m_Texture.Reset();
	m_TextureAllocation->Release(); m_TextureAllocation = nullptr;
	m_IndexBuffer.Reset();
	m_IndexBufferAllocation->Release(); m_IndexBufferAllocation = nullptr;
	m_VertexBuffer.Reset();
	m_VertexBufferAllocation->Release(); m_VertexBufferAllocation = nullptr;
	m_PipelineStateObject.Reset();
	m_RootSignature.Reset();

	CloseHandle(m_FenceEvent);
	m_CommandList.Reset();
	m_CommandQueue.Reset();

	for (size_t i = FRAME_BUFFER_COUNT; i--; )
	{
		m_CbPerObjectUploadHeaps[i].Reset();
		m_CbPerObjectUploadHeapAllocations[i]->Release(); m_CbPerObjectUploadHeapAllocations[i] = nullptr;
		m_MainDescriptorHeap[i].Reset();
		m_ConstantBufferUploadHeap[i].Reset();
		m_ConstantBufferUploadAllocation[i]->Release(); m_ConstantBufferUploadAllocation[i] = nullptr;
	}

	m_DepthStencilDescriptorHeap.Reset();
	m_DepthStencilBuffer.Reset();
	m_DepthStencilAllocation->Release(); m_DepthStencilAllocation = nullptr;
	m_RtvDescriptorHeap.Reset();
	for (size_t i = FRAME_BUFFER_COUNT; i--; )
	{
		m_RenderTargets[i].Reset();
		m_CommandAllocators[i].Reset();
		m_Fences[i].Reset();
	}

	m_Allocator.Reset();
	if (ENABLE_CPU_ALLOCATION_CALLBACKS)
	{
		assert(g_CpuAllocationCount.load() == 0);
	}

	m_Device.Reset();
	m_SwapChain.Reset();
}


void Renderer::OnKeyDown(WPARAM key)
{
	switch (key)
	{
	case 'J':
	{
		WCHAR* statsString = NULL;
		m_Allocator->BuildStatsString(&statsString, TRUE);
		wprintf(L"%s\n", statsString);
		m_Allocator->FreeStatsString(statsString);
	}
	break;

	case VK_ESCAPE:
		PostMessage(Win32Application::GetHwnd(), WM_CLOSE, 0, 0);
		break;
	}
}

// ===========

void Renderer::PrintAdapterInformation(IDXGIAdapter1* adapter)
{
	wprintf(L"DXGI_ADAPTER_DESC1:\n");
	wprintf(L"    Description = %s\n", m_AdapterDesc.Description);
	wprintf(L"    VendorId = 0x%X (%s)\n", m_AdapterDesc.VendorId, VendorIDToStr(m_AdapterDesc.VendorId));
	wprintf(L"    DeviceId = 0x%X\n", m_AdapterDesc.DeviceId);
	wprintf(L"    SubSysId = 0x%X\n", m_AdapterDesc.SubSysId);
	wprintf(L"    Revision = 0x%X\n", m_AdapterDesc.Revision);
	wprintf(L"    DedicatedVideoMemory = %zu B (%s)\n", m_AdapterDesc.DedicatedVideoMemory, SizeToStr(m_AdapterDesc.DedicatedVideoMemory).c_str());
	wprintf(L"    DedicatedSystemMemory = %zu B (%s)\n", m_AdapterDesc.DedicatedSystemMemory, SizeToStr(m_AdapterDesc.DedicatedSystemMemory).c_str());
	wprintf(L"    SharedSystemMemory = %zu B (%s)\n", m_AdapterDesc.SharedSystemMemory, SizeToStr(m_AdapterDesc.SharedSystemMemory).c_str());

	const D3D12_FEATURE_DATA_D3D12_OPTIONS& options = m_Allocator->GetD3D12Options();
	wprintf(L"D3D12_FEATURE_DATA_D3D12_OPTIONS:\n");
	wprintf(L"    StandardSwizzle64KBSupported = %u\n", options.StandardSwizzle64KBSupported ? 1 : 0);
	wprintf(L"    CrossAdapterRowMajorTextureSupported = %u\n", options.CrossAdapterRowMajorTextureSupported ? 1 : 0);
	switch (options.ResourceHeapTier)
	{
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
	if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3))))
	{
		wprintf(L"DXGI_QUERY_VIDEO_MEMORY_INFO:\n");
		for (UINT groupIndex = 0; groupIndex < 2; ++groupIndex)
		{
			const DXGI_MEMORY_SEGMENT_GROUP group = groupIndex == 0 ? DXGI_MEMORY_SEGMENT_GROUP_LOCAL : DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
			const wchar_t* const groupName = groupIndex == 0 ? L"DXGI_MEMORY_SEGMENT_GROUP_LOCAL" : L"DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL";
			DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
			CHECK_HR(adapter3->QueryVideoMemoryInfo(0, group, &info));
			wprintf(L"    %s:\n", groupName);
			wprintf(L"        Budget = %llu B (%s)\n", info.Budget, SizeToStr(info.Budget).c_str());
			wprintf(L"        CurrentUsage = %llu B (%s)\n", info.CurrentUsage, SizeToStr(info.CurrentUsage).c_str());
			wprintf(L"        AvailableForReservation = %llu B (%s)\n", info.AvailableForReservation, SizeToStr(info.AvailableForReservation).c_str());
			wprintf(L"        CurrentReservation = %llu B (%s)\n", info.CurrentReservation, SizeToStr(info.CurrentReservation).c_str());
		}
	}

	assert(m_Device);
	D3D12_FEATURE_DATA_ARCHITECTURE1 architecture1 = {};
	if (SUCCEEDED(m_Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture1, sizeof architecture1)))
	{
		wprintf(L"D3D12_FEATURE_DATA_ARCHITECTURE1:\n");
		wprintf(L"    UMA: %u\n", architecture1.UMA ? 1 : 0);
		wprintf(L"    CacheCoherentUMA: %u\n", architecture1.CacheCoherentUMA ? 1 : 0);
		wprintf(L"    IsolatedMMU: %u\n", architecture1.IsolatedMMU ? 1 : 0);
	}
}
