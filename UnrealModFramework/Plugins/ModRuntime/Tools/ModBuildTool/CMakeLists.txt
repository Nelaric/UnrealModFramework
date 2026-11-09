# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(ModBuildTool LANGUAGES CXX)

add_executable(ModBuildTool
    Source/Main.cpp
    Source/Application.cpp
    Source/Archive.cpp
    Source/Archive.h
    Source/Binary.cpp
    Source/Binary.h
    Source/Build.cpp
    Source/Build.h
    Source/FileSystem.cpp
    Source/FileSystem.h
    Source/Hash.cpp
    Source/Hash.h
    Source/Process.cpp
    Source/Process.h
    Source/Version.h
    include/ModBuildTool/Application.h
)

target_include_directories(ModBuildTool PRIVATE include ../Common)
target_include_directories(ModBuildTool SYSTEM PRIVATE ../ThirdParty/nlohmann/include)

if(WIN32)
    target_compile_definitions(ModBuildTool PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_features(ModBuildTool PRIVATE cxx_std_20)
set_target_properties(ModBuildTool PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

if(MSVC)
    target_compile_options(ModBuildTool PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ModBuildTool PRIVATE -Wall -Wextra -Wpedantic)
endif()
