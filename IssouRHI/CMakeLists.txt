cmake_minimum_required(VERSION 3.30)
project(IssouRHI)

set(CMAKE_CXX_EXTENSIONS OFF)

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

option(BUILD_D3D12_BACKEND "Build the D3D12 backend" ${WIN32})
option(BUILD_METAL_BACKEND "Build the Metal backend" ${APPLE})
# TODO: Vulkan

if(BUILD_D3D12_BACKEND AND NOT WIN32)
    message(FATAL_ERROR "BUILD_D3D12_BACKEND requires Windows")
elseif(BUILD_METAL_BACKEND AND NOT APPLE)
    message(FATAL_ERROR "BUILD_METAL_BACKEND requires Apple")
endif()

if(NOT BUILD_D3D12_BACKEND AND NOT BUILD_METAL_BACKEND)
    message(FATAL_ERROR "At least one backend is required")
endif()

add_library(IssouRHI_common INTERFACE)
target_compile_features(IssouRHI_common INTERFACE cxx_std_23)

if(MSVC)
    target_compile_options(IssouRHI_common INTERFACE /W4 /permissive-)
    target_compile_definitions(IssouRHI_common INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
else()
    # TODO Wall Wextra
endif()

add_library(IssouRHI)

target_sources(IssouRHI
    PRIVATE
        AccelerationStructure.cpp
        Buffer.cpp
        CommandEncoder.cpp
        Device.cpp
        Pipeline.cpp
        QuerySet.cpp
        Queue.cpp
        ShaderTable.cpp
        Surface.cpp
        Texture.cpp
    PUBLIC
        FILE_SET headers
        TYPE HEADERS
        BASE_DIRS
            include
        FILES
            include/IssouRHI.h
            include/InteropD3D12.h
)

target_link_libraries(IssouRHI PRIVATE IssouRHI_common)

if(BUILD_D3D12_BACKEND)
    add_subdirectory(third_party/D3D12MemoryAllocator)

    target_compile_definitions(IssouRHI PUBLIC BUILD_D3D12_BACKEND)

    target_include_directories(IssouRHI
        PUBLIC
            third_party/DirectX-Headers/include/directx
        PRIVATE
            third_party/D3D12MemoryAllocator/include
    )

    target_link_libraries(IssouRHI
        PRIVATE
            D3D12MemoryAllocator
            d3d12.lib
            dxgi.lib
            dxguid.lib
            shlwapi.lib
    )

    target_sources(IssouRHI
        PRIVATE
            D3D12/AccelerationStructureD3D12.cpp
            D3D12/BufferD3D12.cpp
            D3D12/CommandEncoderD3D12.cpp
            D3D12/DescriptorHeapD3D12.cpp
            D3D12/DeviceD3D12.cpp
            D3D12/InteropD3D12.cpp
            D3D12/PipelineD3D12.cpp
            D3D12/QuerySetD3D12.cpp
            D3D12/QueueD3D12.cpp
            D3D12/ShaderTableD3D12.cpp
            D3D12/SurfaceD3D12.cpp
            D3D12/TextureD3D12.cpp
            D3D12/UtilsD3D12.cpp
    )
elseif(BUILD_METAL_BACKEND)
    # TODO
endif()
