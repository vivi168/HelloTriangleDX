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
    add_compile_options(/W4 /MP)

    if(IS_MULTI_CONFIG)
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
    ${SHADERS_DIR}/FillGBufferCS.hlsl
    ${SHADERS_DIR}/FinalComposePS.hlsl
    ${SHADERS_DIR}/FullScreenTriangleVS.hlsl
    ${SHADERS_DIR}/InstanceCullingCS.hlsl
    ${SHADERS_DIR}/MeshletAS.hlsl
    ${SHADERS_DIR}/MeshletMS.hlsl
    ${SHADERS_DIR}/MeshletPS.hlsl
    ${SHADERS_DIR}/SkinningCS.hlsl)

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
    stdafx.cpp
    Win32Application.cpp
    ${THIRD_PARTY_SOURCE})

add_executable(${PROJECT_NAME} ${SOURCES})

target_precompile_headers(${PROJECT_NAME} PRIVATE ${PCH_HEADER})
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
    string(LENGTH "${BASENAME}" BASENAME_LENGTH)
    math(EXPR TYPE_START "${BASENAME_LENGTH} - 2")
    string(SUBSTRING "${BASENAME}" ${TYPE_START} 2 SHADER_TYPE)

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
            /nologo /E main /T ${SHADER_PROFILE}
            /Fo ${CSO}
            /Zi /Qembed_debug ${HLSL}
        DEPENDS ${HLSL} ${HLSL_HEADERS})
    list(APPEND SHADER_BINARY_FILES ${CSO})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SHADER_BINARY_FILES})
add_dependencies(HelloTriangleDX compile_shaders)
