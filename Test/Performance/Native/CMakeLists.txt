# Copyright (c) 2026 Nelaric Contributors
cmake_minimum_required(VERSION 3.24)
project(BenchmarkExtension LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_library(BenchmarkExtension SHARED BenchmarkExtension.cpp)
target_include_directories(BenchmarkExtension PRIVATE "${UMF_ABI_INCLUDE}")
