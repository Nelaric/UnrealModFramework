# Copyright (c) 2026 Nelaric Contributors
cmake_minimum_required(VERSION 3.24)
project(SharedRuntimeExtensions LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

if(APPLE)
    add_link_options("LINKER:-undefined,error")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options("LINKER:-z,defs")
endif()

foreach(name IN ITEMS TestMod SharedCaller)
    add_library(${name}Extension SHARED SharedHook.cpp)
    target_include_directories(${name}Extension PRIVATE "${UMF_SDK_INCLUDE}")
endforeach()
target_compile_definitions(TestModExtension PRIVATE HEALTH_DELTA=4)
target_compile_definitions(SharedCallerExtension PRIVATE HEALTH_DELTA=6)
