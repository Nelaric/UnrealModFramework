# Copyright (c) 2026 Nelaric Contributors
cmake_minimum_required(VERSION 3.24)
project(BenchmarkExtension LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(APPLE)
    add_link_options("LINKER:-undefined,error")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options("LINKER:-z,defs")
endif()

add_library(BenchmarkExtension SHARED BenchmarkExtension.cpp)
set_target_properties(BenchmarkExtension PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
)
target_include_directories(BenchmarkExtension PRIVATE "${UMF_ABI_INCLUDE}")
