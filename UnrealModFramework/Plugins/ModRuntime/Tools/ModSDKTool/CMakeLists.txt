# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(ModSDKTool LANGUAGES CXX)

add_executable(ModSDKTool
    Source/Main.cpp
    Source/Application.cpp
    Source/Manifest.cpp
    Source/Generator.cpp
    Source/Build.cpp
    Source/Process.cpp
    Source/Sdk.h
    include/ModSDKTool/Application.h
    ../ModBuildTool/Source/FileSystem.cpp
    ../ModBuildTool/Source/Hash.cpp
)

target_include_directories(ModSDKTool PRIVATE include ../ModBuildTool/Source)
target_include_directories(ModSDKTool SYSTEM PRIVATE ../ThirdParty/nlohmann/include)

if(WIN32)
    target_compile_definitions(ModSDKTool PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_features(ModSDKTool PRIVATE cxx_std_20)
set_target_properties(ModSDKTool PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

add_custom_command(TARGET ModSDKTool POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:ModSDKTool>/Licenses"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../LICENSE"
        "$<TARGET_FILE_DIR:ModSDKTool>/Licenses/UnrealModFramework.LICENSE"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/nlohmann/LICENSE.MIT"
        "$<TARGET_FILE_DIR:ModSDKTool>/Licenses/nlohmann.LICENSE.MIT"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../NOTICE.md"
        "$<TARGET_FILE_DIR:ModSDKTool>/Licenses/NOTICE.md"
    VERBATIM
)

if(MSVC)
    target_compile_options(ModSDKTool PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ModSDKTool PRIVATE -Wall -Wextra -Wpedantic)
endif()
