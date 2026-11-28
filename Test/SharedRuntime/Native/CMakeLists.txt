# Copyright (c) 2026 Nelaric Contributors
cmake_minimum_required(VERSION 3.24)
project(SharedRuntimeExtensions LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

foreach(name IN ITEMS TestMod SharedCaller)
    add_library(${name}Extension SHARED SharedHook.cpp)
    target_include_directories(${name}Extension PRIVATE "${UMF_SDK_INCLUDE}")
endforeach()
target_compile_definitions(TestModExtension PRIVATE HEALTH_DELTA=4)
target_compile_definitions(SharedCallerExtension PRIVATE HEALTH_DELTA=6)
