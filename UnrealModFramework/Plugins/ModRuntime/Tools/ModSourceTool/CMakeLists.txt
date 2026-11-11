# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(ModSourceTool LANGUAGES CXX)

if(NOT TARGET ModToolSupport)
    add_subdirectory(../Common ModToolSupport)
endif()

add_executable(ModSourceTool
    src/main.cpp
    src/Analysis/Analyzer.cpp
    src/Analysis/Analyzer.h
    src/Analysis/ArgumentValue.cpp
    src/Analysis/ArgumentValue.h
    src/Analysis/BodyAnalysis.cpp
    src/Analysis/BodyAnalysis.h
    src/Analysis/Guard.cpp
    src/Analysis/Guard.h
    src/Analysis/GuardClassification.cpp
    src/Analysis/GuardClassification.h
    src/Analysis/Location.cpp
    src/Analysis/Location.h
    src/Analysis/SourceAnalyzer.cpp
    src/Analysis/SourceAnalyzer.h
    src/Analysis/SourceRequest.cpp
    src/Analysis/SourceRequest.h
    src/Analysis/Trace.cpp
    src/Analysis/Trace.h
    src/Application.cpp
    src/Application.h
    src/Assembly/AssemblyAnalysis.cpp
    src/Assembly/AssemblyAnalysis.h
    src/Assembly/AssemblyCalls.cpp
    src/Assembly/AssemblyCalls.h
    src/Assembly/AssemblyCompiler.cpp
    src/Assembly/AssemblyCompiler.h
    src/Assembly/AssemblyEvidence.cpp
    src/Assembly/AssemblyEvidence.h
    src/Assembly/AssemblyFiles.cpp
    src/Assembly/AssemblyFiles.h
    src/Assembly/AssemblyFlow.cpp
    src/Assembly/AssemblyFlow.h
    src/Assembly/AssemblyListing.cpp
    src/Assembly/AssemblyListing.h
    src/Assembly/DebugLocations.cpp
    src/Assembly/DebugLocations.h
    src/Assembly/FlagBranches.cpp
    src/Assembly/FlagBranches.h
    src/Assembly/InlineSite.cpp
    src/Assembly/InlineSite.h
    src/Assembly/Instruction.cpp
    src/Assembly/Instruction.h
    src/Assembly/ListingText.cpp
    src/Assembly/ListingText.h
    src/Assembly/ValidityBranches.cpp
    src/Assembly/ValidityBranches.h
    src/Assembly/ValidityMatcher.cpp
    src/Assembly/ValidityMatcher.h
    src/Clang/Clang.cpp
    src/Clang/Clang.h
    src/Clang/CompilationDatabase.cpp
    src/Clang/CompilationDatabase.h
    src/Clang/CompileCommand.cpp
    src/Clang/CompileCommand.h
    src/Clang/TranslationUnitParser.cpp
    src/Clang/TranslationUnitParser.h
    src/Cli/AnalysisCommand.cpp
    src/Cli/AnalysisCommand.h
    src/Cli/AnalysisOptions.cpp
    src/Cli/AnalysisOptions.h
    src/Cli/AnalysisReport.cpp
    src/Cli/AnalysisReport.h
    src/Cli/CommandLine.cpp
    src/Cli/CommandLine.h
    src/Cli/HostLinkHeader.cpp
    src/Cli/HostLinkHeader.h
    src/Errors/CatalogText.cpp
    src/Errors/CatalogText.h
    src/Errors/ErrorBindings.cpp
    src/Errors/ErrorBindings.h
    src/Errors/ErrorCatalog.cpp
    src/Errors/ErrorCatalog.h
    src/Errors/ErrorCodePolicy.cpp
    src/Errors/ErrorCodePolicy.h
    src/Errors/PendingBindings.cpp
    src/Errors/PendingBindings.h
)

target_include_directories(ModSourceTool SYSTEM PRIVATE
    ../ThirdParty/clang/include
    ../ThirdParty/nlohmann/include
)
target_link_libraries(ModSourceTool PRIVATE ModToolSupport ${CMAKE_DL_LIBS})
if(WIN32)
    target_compile_definitions(ModSourceTool PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_features(ModSourceTool PRIVATE cxx_std_20)
set_target_properties(ModSourceTool PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

if(MSVC)
    target_compile_options(ModSourceTool PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ModSourceTool PRIVATE -Wall -Wextra -Wpedantic)
endif()
