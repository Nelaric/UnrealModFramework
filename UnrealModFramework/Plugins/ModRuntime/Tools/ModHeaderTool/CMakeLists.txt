# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(ModHeaderTool LANGUAGES CXX)

if(NOT TARGET ModToolSupport)
    add_subdirectory(../Common ModToolSupport)
endif()

add_library(ModHeaderToolCore STATIC
    src/Analysis/AnalysisArguments.cpp
    src/Analysis/AnalysisArguments.h
    src/Analysis/ClangLibrary.cpp
    src/Analysis/ClangLibrary.h
    src/Analysis/CursorSource.cpp
    src/Analysis/CursorSource.h
    src/Analysis/DeclarationAnalyzer.cpp
    src/Analysis/DeclarationAnalyzer.h
    src/Analysis/DeclarationBinding.cpp
    src/Analysis/DeclarationBinding.h
    src/Analysis/EngineHeader.cpp
    src/Analysis/EngineHeader.h
    src/Analysis/ExtensionBinding.cpp
    src/Analysis/ExtensionBinding.h
    src/Analysis/FullView.cpp
    src/Analysis/FullView.h
    src/Analysis/FullViewSelection.cpp
    src/Analysis/FullViewSelection.h
    src/Analysis/Location.cpp
    src/Analysis/Location.h
    src/Analysis/Source.cpp
    src/Analysis/Source.h
    src/Analysis/SourceErrorAnalyzer.cpp
    src/Analysis/SourceErrorAnalyzer.h
    src/Analysis/SourceReport.cpp
    src/Analysis/SourceReport.h
    src/Analysis/SourceReportBinder.cpp
    src/Analysis/SourceReportBinder.h
    src/Analysis/SourceToolInvocation.cpp
    src/Analysis/SourceToolInvocation.h
    src/Analysis/Traversal.cpp
    src/Analysis/Traversal.h
    src/Analysis/TypeResolution.cpp
    src/Analysis/TypeResolution.h
    src/Application.cpp
    src/Application.h
    src/Cli/CommandLine.cpp
    src/Cli/CommandLine.h
    src/Cli/OptionsCheck.cpp
    src/Cli/OptionsCheck.h
    src/Cli/ScanCommand.cpp
    src/Cli/ScanCommand.h
    src/Files/DiagnosticLocation.cpp
    src/Files/DiagnosticLocation.h
    src/Files/Path.cpp
    src/Files/Path.h
    src/Files/TextFile.cpp
    src/Files/TextFile.h
    src/Generation/Abi/NativeABIGenerator.cpp
    src/Generation/Abi/NativeABIGenerator.h
    src/Generation/Abi/NativeCode.cpp
    src/Generation/Abi/NativeCode.h
    src/Generation/Abi/NativeSignature.cpp
    src/Generation/Abi/NativeSignature.h
    src/Generation/API.cpp
    src/Generation/API.h
    src/Generation/BlueprintExports.cpp
    src/Generation/BlueprintExports.h
    src/Generation/APIGenerator.cpp
    src/Generation/APIGenerator.h
    src/Generation/APIParameter.cpp
    src/Generation/APIParameter.h
    src/Generation/APIType.cpp
    src/Generation/APIType.h
    src/Generation/Artifacts/ArtifactEmitter.cpp
    src/Generation/Artifacts/ArtifactEmitter.h
    src/Generation/Artifacts/ArtifactRenderer.cpp
    src/Generation/Artifacts/ArtifactRenderer.h
    src/Generation/Artifacts/CppSignature.cpp
    src/Generation/Artifacts/CppSignature.h
    src/Generation/Artifacts/ExtensionWriter.cpp
    src/Generation/Artifacts/ExtensionWriter.h
    src/Generation/Artifacts/GeneratedBatch.cpp
    src/Generation/Artifacts/GeneratedBatch.h
    src/Generation/Artifacts/GeneratedSource.cpp
    src/Generation/Artifacts/GeneratedSource.h
    src/Generation/Artifacts/GenerationDestination.cpp
    src/Generation/Artifacts/GenerationDestination.h
    src/Generation/Artifacts/GenerationManifest.cpp
    src/Generation/Artifacts/GenerationManifest.h
    src/Generation/Artifacts/GenerationOutput.cpp
    src/Generation/Artifacts/GenerationOutput.h
    src/Generation/Artifacts/GenerationPaths.cpp
    src/Generation/Artifacts/GenerationPaths.h
    src/Generation/Artifacts/HostBridgeWriter.cpp
    src/Generation/Artifacts/HostBridgeWriter.h
    src/Generation/Artifacts/InputIndex.cpp
    src/Generation/Artifacts/InputIndex.h
    src/Generation/Artifacts/LifecycleExport.cpp
    src/Generation/Artifacts/LifecycleExport.h
    src/Generation/Artifacts/OutputFiles.cpp
    src/Generation/Artifacts/OutputFiles.h
    src/Generation/Artifacts/OutputIndex.cpp
    src/Generation/Artifacts/OutputIndex.h
    src/Generation/Artifacts/OutputLock.cpp
    src/Generation/Artifacts/OutputLock.h
    src/Generation/Artifacts/OutputPaths.cpp
    src/Generation/Artifacts/OutputPaths.h
    src/Generation/Artifacts/Projection.cpp
    src/Generation/Artifacts/Projection.h
    src/Generation/Artifacts/ProjectionWriter.cpp
    src/Generation/Artifacts/ProjectionWriter.h
    src/Generation/Artifacts/SdkViewWriter.cpp
    src/Generation/Artifacts/SdkViewWriter.h
    src/Generation/Extension.cpp
    src/Generation/Extension.h
    src/Generation/ExportNameRules.cpp
    src/Generation/ExportNameRules.h
    src/Generation/ExportNames.cpp
    src/Generation/ExportNames.h
    src/Generation/ExportRedirects.cpp
    src/Generation/ExportRedirects.h
    src/Generation/Generation.cpp
    src/Generation/Generation.h
    src/Generation/GenerationFields.cpp
    src/Generation/GenerationFields.h
    src/Generation/GenerationInput.cpp
    src/Generation/GenerationInput.h
    src/Generation/GenerationInputReader.cpp
    src/Generation/GenerationInputReader.h
    src/Generation/GenerationModel.cpp
    src/Generation/GenerationModel.h
    src/Generation/ModuleInput.cpp
    src/Generation/ModuleInput.h
    src/Generation/ResolvedType.cpp
    src/Generation/ResolvedType.h
    src/Generation/TypeIdentity.cpp
    src/Generation/TypeIdentity.h
    src/Hash/Hash.cpp
    src/Hash/Hash.h
    src/Hash/Sha256.cpp
    src/Hash/Sha256.h
    src/Metadata/FunctionRedirect.cpp
    src/Metadata/FunctionRedirect.h
    src/Metadata/MetadataField.cpp
    src/Metadata/MetadataField.h
    src/Metadata/MetadataName.cpp
    src/Metadata/MetadataName.h
    src/Metadata/MetadataOptions.cpp
    src/Metadata/MetadataOptions.h
    src/Metadata/MetadataOptionsReader.cpp
    src/Metadata/MetadataOptionsReader.h
    src/Metadata/MetadataOptionsRefreshRequired.cpp
    src/Metadata/MetadataOptionsRefreshRequired.h
    src/Metadata/MetadataParser.cpp
    src/Metadata/MetadataParser.h
    src/Metadata/MetadataSnapshot.cpp
    src/Metadata/MetadataSnapshot.h
    src/Metadata/MetadataValidator.cpp
    src/Metadata/MetadataValidator.h
    src/Metadata/MetadataValue.cpp
    src/Metadata/MetadataValue.h
    src/Metadata/Parser.cpp
    src/Metadata/Parser.h
    src/Metadata/RedirectFunctionName.cpp
    src/Metadata/RedirectFunctionName.h
    src/Metadata/RedirectReader.cpp
    src/Metadata/RedirectReader.h
    src/Metadata/Validator.cpp
    src/Metadata/Validator.h
    src/Project/Project.cpp
    src/Project/Project.h
    src/Project/ProjectDirectories.cpp
    src/Project/ProjectDirectories.h
    src/Project/ProjectReader.cpp
    src/Project/ProjectReader.h
    src/Project/ProjectSources.cpp
    src/Project/ProjectSources.h
    src/Scan/ExportMacro.cpp
    src/Scan/ExportMacro.h
    src/Scan/Lexer.cpp
    src/Scan/Lexer.h
    src/Scan/MacroNames.cpp
    src/Scan/MacroNames.h
    src/Scan/MacroScanner.cpp
    src/Scan/MacroScanner.h
    src/Scan/PendingMacro.cpp
    src/Scan/PendingMacro.h
    src/Scan/ProjectScan.cpp
    src/Scan/ProjectScan.h
    src/Scan/ProjectScanner.cpp
    src/Scan/ProjectScanner.h
    src/Scan/ScanReport.cpp
    src/Scan/ScanReport.h
    src/Scan/ScanResult.cpp
    src/Scan/ScanResult.h
    src/Scan/SourceCharacter.cpp
    src/Scan/SourceCharacter.h
    src/Scan/Token.cpp
    src/Scan/Token.h
)

target_include_directories(ModHeaderToolCore PRIVATE
    ../../Source/Runtime/ExportMetadata/Public
)
target_include_directories(ModHeaderToolCore SYSTEM PRIVATE
    ../ThirdParty/nlohmann/include
    ../ThirdParty/clang/include
)
target_link_libraries(ModHeaderToolCore PUBLIC ModToolSupport PRIVATE ${CMAKE_DL_LIBS})
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
if(NOT TARGET ModSourceTool)
    add_subdirectory(../ModSourceTool ModSourceTool)
endif()
add_dependencies(ModHeaderTool ModSourceTool)

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
