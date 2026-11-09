# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(UnrealModFrameworkTool LANGUAGES CXX)

if(DEFINED UMF_EXPECTED_COMPILER_ID AND NOT CMAKE_CXX_COMPILER_ID STREQUAL UMF_EXPECTED_COMPILER_ID)
    message(FATAL_ERROR
        "Expected compiler '${UMF_EXPECTED_COMPILER_ID}', but '${CMAKE_CXX_COMPILER_ID}' was detected. "
        "Windows builds require MSVC; Unix builds require GNU GCC. On macOS select GNU g++ via CXX."
    )
endif()

add_subdirectory(ModBuildTool)
add_subdirectory(ModHeaderTool)
add_subdirectory(ModSDKTool)
