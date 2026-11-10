# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(ModBuildTool LANGUAGES CXX)

if(NOT TARGET ModToolSupport)
    add_subdirectory(../Common ModToolSupport)
endif()

add_executable(ModBuildTool
    src/main.cpp
    src/Application.cpp
    src/Application.h
    src/Archive/ArchiveInspector.cpp
    src/Archive/ArchiveInspector.h
    src/Archive/ArchiveReader.cpp
    src/Archive/ArchiveReader.h
    src/Binary/BinaryInspector.cpp
    src/Binary/BinaryInspector.h
    src/Binary/ImageReader.cpp
    src/Binary/ImageReader.h
    src/Build/ArchiveWitness.cpp
    src/Build/ArchiveWitness.h
    src/Build/BuildInputs.cpp
    src/Build/BuildInputs.h
    src/Build/BuildRecipe.cpp
    src/Build/BuildRecipe.h
    src/Build/CompiledObjects.cpp
    src/Build/CompiledObjects.h
    src/Build/CompilerDependencies.cpp
    src/Build/CompilerDependencies.h
    src/Build/ContextDocument.cpp
    src/Build/ContextDocument.h
    src/Build/HostBuild.cpp
    src/Build/HostBuild.h
    src/Build/HostCompiler.cpp
    src/Build/HostCompiler.h
    src/Build/HostContext.cpp
    src/Build/HostContext.h
    src/Build/HostPlan.cpp
    src/Build/HostPlan.h
    src/Build/HostPublisher.cpp
    src/Build/HostPublisher.h
    src/Build/LinkReceiptVerifier.cpp
    src/Build/LinkReceiptVerifier.h
    src/Build/OwnedStage.cpp
    src/Build/OwnedStage.h
    src/Build/RecipeValidation.cpp
    src/Build/RecipeValidation.h
    src/Build/Request.cpp
    src/Build/Request.h
    src/Build/Unreal/UeBuildAdapter.cpp
    src/Build/Unreal/UeBuildAdapter.h
    src/Build/Unreal/UeTargetReceipt.cpp
    src/Build/Unreal/UeTargetReceipt.h
    src/Cli/CommandArguments.cpp
    src/Cli/CommandArguments.h
    src/Cli/CommandLine.cpp
    src/Cli/CommandLine.h
    src/Cli/HashCommand.cpp
    src/Cli/HashCommand.h
    src/Cli/HostCommandResult.cpp
    src/Cli/HostCommandResult.h
    src/Cli/HostCommands.cpp
    src/Cli/HostCommands.h
    src/Cli/InspectionCommands.cpp
    src/Cli/InspectionCommands.h
    src/Cli/UeCommands.cpp
    src/Cli/UeCommands.h
    src/Cli/Version.cpp
    src/Cli/Version.h
)

target_include_directories(ModBuildTool SYSTEM PRIVATE ../ThirdParty/nlohmann/include)

target_link_libraries(ModBuildTool PRIVATE ModToolSupport)

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
