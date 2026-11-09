# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(ModHeaderTool LANGUAGES CXX)

add_library(ModHeaderToolCore STATIC
    src/Application.cpp
    src/ApiGenerator.cpp
    src/NativeAbiGenerator.cpp
    src/NativeAbiGenerator.h
    src/ClangLibrary.cpp
    src/ClangLibrary.h
    src/DeclarationAnalyzer.cpp
    src/GenerationInput.cpp
    src/GenerationOutput.cpp
    src/GenerationOutput.h
    src/GenerationModel.h
    src/Hash.cpp
    src/Hash.h
    src/CommandLine.cpp
    src/FileSystem.cpp
    src/MacroScanner.cpp
    src/SourceLexer.cpp
    src/SourceLexer.h
    src/MetadataOptions.cpp
    src/MetadataParser.cpp
    src/MetadataValidator.cpp
    src/Project.cpp
    src/Scanner.cpp
    src/ScanReport.cpp
    include/ModHeaderTool/Application.h
    include/ModHeaderTool/Generation.h
    include/ModHeaderTool/CommandLine.h
    include/ModHeaderTool/FileSystem.h
    include/ModHeaderTool/MacroScanner.h
    include/ModHeaderTool/Metadata.h
    include/ModHeaderTool/MetadataOptions.h
    include/ModHeaderTool/MetadataParser.h
    include/ModHeaderTool/MetadataValidator.h
    include/ModHeaderTool/Project.h
    include/ModHeaderTool/Scanner.h
    include/ModHeaderTool/ScanReport.h
)

target_include_directories(ModHeaderToolCore PUBLIC include)
target_include_directories(ModHeaderToolCore PRIVATE
    ../../Source/Runtime/ExportMetadata/Public
    ../Common
)
target_include_directories(ModHeaderToolCore SYSTEM PRIVATE
    ../ThirdParty/nlohmann/include
    ../ThirdParty/clang/include
)
target_link_libraries(ModHeaderToolCore PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
    target_compile_definitions(ModHeaderToolCore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_compile_features(ModHeaderToolCore PUBLIC cxx_std_20)
set_target_properties(ModHeaderToolCore PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

add_executable(ModHeaderTool src/main.cpp)
target_link_libraries(ModHeaderTool PRIVATE ModHeaderToolCore)

set_target_properties(ModHeaderTool PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

if(MSVC)
    target_compile_options(ModHeaderToolCore PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(ModHeaderTool PRIVATE /W4 /permissive- /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ModHeaderToolCore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ModHeaderTool PRIVATE -Wall -Wextra -Wpedantic)
endif()
