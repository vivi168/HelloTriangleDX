cmake_minimum_required(VERSION 3.30)
project(HelloTriangleDX)

set(CMAKE_CXX_EXTENSIONS OFF)

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

add_library(common INTERFACE)
target_compile_features(common INTERFACE cxx_std_23)
if(MSVC)
    target_compile_options(common INTERFACE /W4 /permissive-)
    target_compile_definitions(common INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()

add_subdirectory(third_party/D3D12MemoryAllocator)
add_subdirectory(third_party/DirectXMesh)
add_subdirectory(third_party/DirectXTex)

add_library(imgui)
target_sources(imgui
    PRIVATE
        third_party/imgui/imgui_demo.cpp
        third_party/imgui/imgui_draw.cpp
        third_party/imgui/imgui_tables.cpp
        third_party/imgui/imgui_widgets.cpp
        third_party/imgui/imgui.cpp
        third_party/imgui/backends/imgui_impl_dx12.cpp
        third_party/imgui/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui
    PUBLIC
        third_party/imgui
        third_party/imgui/backends
)
target_link_libraries(imgui PRIVATE common)

add_library(IssouRHI)
target_sources(IssouRHI
    PRIVATE
        IssouRHI/AccelerationStructure.cpp
        IssouRHI/Buffer.cpp
        IssouRHI/CommandEncoder.cpp
        IssouRHI/DescriptorHeap.cpp
        IssouRHI/Device.cpp
        IssouRHI/Pipeline.cpp
        IssouRHI/QuerySet.cpp
        IssouRHI/Queue.cpp
        IssouRHI/Surface.cpp
        IssouRHI/Texture.cpp
        IssouRHI/Utils.cpp
    PUBLIC
        FILE_SET headers
        TYPE HEADERS
        BASE_DIRS
            IssouRHI/include
        FILES
            IssouRHI/include/IssouRHI.h
)
target_include_directories(IssouRHI
    PUBLIC # PRIVATE TODO: make these private when we remove these include from IssouRHI.h
        third_party/DirectX-Headers/include/directx
        third_party/D3D12MemoryAllocator/include
)
target_link_libraries(IssouRHI
    PRIVATE
        common
        D3D12MemoryAllocator
        d3d12.lib
        dxgi.lib
        dxguid.lib
        shlwapi.lib
)

set(SHADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(HLSL_SOURCES
    ${SHADERS_DIR}/FillGBuffer.cs.hlsl
    ${SHADERS_DIR}/FinalCompose.ps.hlsl
    ${SHADERS_DIR}/FullScreenTriangle.vs.hlsl
    ${SHADERS_DIR}/InstanceCulling.cs.hlsl
    ${SHADERS_DIR}/Meshlet.as.hlsl
    ${SHADERS_DIR}/Meshlet.ms.hlsl
    ${SHADERS_DIR}/Meshlet.ps.hlsl
    ${SHADERS_DIR}/RayTracing.rt.hlsl
    ${SHADERS_DIR}/Skinning.cs.hlsl
)
set(HLSL_HEADERS
    ${SHADERS_DIR}/MeshletCommon.hlsli
    ${SHADERS_DIR}/VisibilityBufferCommon.hlsli
)
set(SHARED_HEADERS
    ${SHADERS_DIR}/Shared.h
)

add_executable(HelloTriangleDX)
target_sources(HelloTriangleDX
    PRIVATE
        Camera.cpp
        Collider.cpp
        Game.cpp
        Input.cpp
        Main.cpp
        Mesh.cpp
        Renderer.cpp
        Win32Application.cpp
        # HEADERS
        Camera.h
        Collider.h
        Game.h
        Input.h
        Mesh.h
        Renderer.h
        RendererHelper.h
        StepTimer.h
        Win32Application.h
)
target_precompile_headers(HelloTriangleDX PRIVATE stdafx.h)
target_include_directories(HelloTriangleDX
    PRIVATE
        third_party/DirectX-Headers/include/directx # TODO remove when everything is done through the rhi
        third_party/DirectXMesh/DirectXMesh
        third_party/DirectXTex/DirectXTex
)
target_link_libraries(HelloTriangleDX
    PRIVATE
        common
        DirectXMesh
        DirectXTex
        imgui
        IssouRHI
        d3d12.lib # TODO: remove when everything dx12 is done through the rhi
)

find_program(DXC_EXECUTABLE dxc.exe)

if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "dxc.exe not found")
endif()

function(get_shader_profile BASENAME OUTPUT_PROFILE)
    get_filename_component(SHADER_EXT ${BASENAME} EXT)
    string(REGEX REPLACE "^\\." "" SHADER_TYPE ${SHADER_EXT})
    string(TOUPPER "${SHADER_TYPE}" SHADER_TYPE)

    if(SHADER_TYPE STREQUAL "PS")
        set(SM_TYPE ps)
    elseif(SHADER_TYPE STREQUAL "VS")
        set(SM_TYPE vs)
    elseif(SHADER_TYPE STREQUAL "CS")
        set(SM_TYPE cs)
    elseif(SHADER_TYPE STREQUAL "MS")
        set(SM_TYPE ms)
    elseif(SHADER_TYPE STREQUAL "AS")
        set(SM_TYPE as)
    elseif(SHADER_TYPE STREQUAL "RT")
        set(SM_TYPE lib)
    else()
        message(FATAL_ERROR "${SHADER_TYPE} is not a recognized shader type")
    endif()

    set(${OUTPUT_PROFILE} "${SM_TYPE}_6_6" PARENT_SCOPE)
endfunction()

if (IS_MULTI_CONFIG)
    set(SHADERS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/$<CONFIG>")
else()
    set(SHADERS_OUTPUT_DIR ${CMAKE_BINARY_DIR})
endif()

foreach(HLSL ${HLSL_SOURCES})
    get_filename_component(STEM ${HLSL} NAME_WLE)
    get_shader_profile(${STEM} SHADER_PROFILE)
    set(CSO "${SHADERS_OUTPUT_DIR}/${STEM}.cso")
    add_custom_command(
        OUTPUT ${CSO}
        COMMAND ${DXC_EXECUTABLE}
            /nologo /T ${SHADER_PROFILE} # assume /E main
            /Fo ${CSO}
            /Zi /Qembed_debug ${HLSL}
        DEPENDS ${HLSL} ${HLSL_HEADERS} ${SHARED_HEADERS})
    list(APPEND SHADER_BINARY_FILES ${CSO})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SHADER_BINARY_FILES})
add_dependencies(HelloTriangleDX compile_shaders)
