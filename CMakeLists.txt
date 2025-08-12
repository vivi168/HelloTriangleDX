cmake_minimum_required(VERSION 3.30)
project(HelloTriangleDX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(IS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "")
endif()

if(MSVC)
    add_compile_options(/W4 /MP /permissive-)

    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        add_compile_options("$<$<CONFIG:RELEASE>:/Zi>")
        add_link_options("$<$<CONFIG:RELEASE>:/DEBUG>")
    endif()
endif()

add_subdirectory(third_party/DirectXMesh)
add_subdirectory(third_party/DirectXTex)

set(IMGUI_SOURCES
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx12.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp)

set(D3D12MA_SOURCES
    third_party/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp)

set(THIRD_PARTY_SOURCE ${IMGUI_SOURCES} ${D3D12MA_SOURCES})

set(SHADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(HLSL_SOURCES
    ${SHADERS_DIR}/FillGBuffer.cs.hlsl
    ${SHADERS_DIR}/FinalCompose.ps.hlsl
    ${SHADERS_DIR}/FullScreenTriangle.vs.hlsl
    ${SHADERS_DIR}/InstanceCulling.cs.hlsl
    ${SHADERS_DIR}/Meshlet.as.hlsl
    ${SHADERS_DIR}/Meshlet.ms.hlsl
    ${SHADERS_DIR}/Meshlet.ps.hlsl
    ${SHADERS_DIR}/Skinning.cs.hlsl
    ${SHADERS_DIR}/RayTracing.rt.hlsl)

set(HLSL_HEADERS
    ${SHADERS_DIR}/MeshletCommon.hlsli
    ${SHADERS_DIR}/Shared.h
    ${SHADERS_DIR}/VisibilityBufferCommon.hlsli)

set(SOURCES
    Camera.cpp
    Collider.cpp
    Game.cpp
    Input.cpp
    Main.cpp
    Mesh.cpp
    Renderer.cpp
    Win32Application.cpp
    ${THIRD_PARTY_SOURCE})

set(HEADERS
    Camera.h
    Collider.h
    DescriptorHeapListAllocator.h
    Game.h
    GpuBuffer.h
    HeapDescriptor.h
    Input.h
    Mesh.h
    Renderer.h
    RendererHelper.h
    Win32Application.h
    ${SHADERS_DIR}/Shared.h)

set(STDAFX_HEADER stdafx.h)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${STDAFX_HEADER})

target_precompile_headers(${PROJECT_NAME} PRIVATE ${STDAFX_HEADER})
set_source_files_properties(${THIRD_PARTY_SOURCE} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_include_directories(HelloTriangleDX PRIVATE
    third_party/D3D12MemoryAllocator/include
    third_party/DirectX-Headers/include/directx
    third_party/DirectXMesh/DirectXMesh
    third_party/DirectXTex/DirectXTex
    third_party/imgui
    third_party/imgui/backends)

target_link_libraries(HelloTriangleDX PRIVATE
    d3d12.lib
    dxgi.lib
    shlwapi.lib
    DirectXMesh
    DirectXTex)

target_compile_definitions(HelloTriangleDX PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)

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
        DEPENDS ${HLSL} ${HLSL_HEADERS})
    list(APPEND SHADER_BINARY_FILES ${CSO})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SHADER_BINARY_FILES})
add_dependencies(HelloTriangleDX compile_shaders)
