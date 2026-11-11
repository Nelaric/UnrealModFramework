# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(UnrealModFrameworkTool LANGUAGES CXX)

if(DEFINED UMF_EXPECTED_COMPILER_ID AND NOT CMAKE_CXX_COMPILER_ID STREQUAL UMF_EXPECTED_COMPILER_ID)
    message(FATAL_ERROR
        "Expected compiler '${UMF_EXPECTED_COMPILER_ID}', but '${CMAKE_CXX_COMPILER_ID}' was detected. "
        "Windows builds require MSVC; Unix builds require GNU GCC. On macOS select GNU g++ via CXX."
    )
endif()

if(NOT DEFINED UMF_RELEASE_TOOL_OUTPUT_ROOT AND CMAKE_BINARY_DIR STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}/build")
    set(UMF_RELEASE_TOOL_OUTPUT_ROOT "${CMAKE_BINARY_DIR}")
endif()

foreach(tool IN ITEMS ModBuildTool ModSourceTool ModHeaderTool ModSDKTool)
    add_subdirectory(${tool})
    if(UMF_RELEASE_TOOL_OUTPUT_ROOT)
        set_target_properties(${tool} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_RELEASE "${UMF_RELEASE_TOOL_OUTPUT_ROOT}/${tool}"
        )
    endif()
endforeach()

add_custom_target(ModRuntimeUhtTools)
add_dependencies(ModRuntimeUhtTools ModBuildTool ModHeaderTool ModSourceTool)
