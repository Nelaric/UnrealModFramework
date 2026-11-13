# Copyright (c) 2026 Nelaric Contributors

cmake_minimum_required(VERSION 3.20)

project(ModSDKTool LANGUAGES CXX)

if(NOT TARGET ModToolSupport)
    add_subdirectory(../Common ModToolSupport)
endif()

add_executable(ModSDKTool
    src/main.cpp
    src/Application.cpp
    src/Application.h
    src/Build/BuildProgress.cpp
    src/Build/BuildProgress.h
    src/Build/NativeBuild.cpp
    src/Build/NativeBuild.h
    src/Build/PathRelations.cpp
    src/Build/PathRelations.h
    src/Build/SdkBuild.cpp
    src/Build/SdkBuild.h
    src/Build/SdkInputCheck.cpp
    src/Build/SdkInputCheck.h
    src/Build/SdkRequest.cpp
    src/Build/SdkRequest.h
    src/Build/StageFiles.cpp
    src/Build/StageFiles.h
    src/Build/Workspace.cpp
    src/Build/Workspace.h
    src/Bundle/BundleAssembly.cpp
    src/Bundle/BundleAssembly.h
    src/Bundle/BundleInventory.cpp
    src/Bundle/BundleInventory.h
    src/Bundle/BundlePublisher.cpp
    src/Bundle/BundlePublisher.h
    src/Bundle/BundleReuse.cpp
    src/Bundle/BundleReuse.h
    src/Bundle/ClientLayout.cpp
    src/Bundle/ClientLayout.h
    src/Bundle/SdkBundle.cpp
    src/Bundle/SdkBundle.h
    src/Cli/CommandDiagnostics.cpp
    src/Cli/CommandDiagnostics.h
    src/Cli/CommandLine.cpp
    src/Cli/CommandLine.h
    src/Cli/CommandOptions.cpp
    src/Cli/CommandOptions.h
    src/Cli/EditorBuildCommand.cpp
    src/Cli/EditorBuildCommand.h
    src/Cli/EditorBuildOptions.cpp
    src/Cli/EditorBuildOptions.h
    src/Cli/ResultOutput.cpp
    src/Cli/ResultOutput.h
    src/Cli/SdkBuildCommands.cpp
    src/Cli/SdkBuildCommands.h
    src/Cli/SdkInspectCommand.cpp
    src/Cli/SdkInspectCommand.h
    src/Editor/DevelopmentEditorResources.h
    src/Editor/EditorBuild.cpp
    src/Editor/EditorBuild.h
    src/Editor/EditorPackage.cpp
    src/Editor/EditorPackage.h
    src/Editor/EditorPlugin.cpp
    src/Editor/EditorPlugin.h
    src/Editor/PluginPackages.cpp
    src/Editor/PluginPackages.h
    src/Editor/PluginSources.cpp
    src/Editor/PluginSources.h
    src/Generation/CppLiteral.cpp
    src/Generation/CppLiteral.h
    src/Generation/GeneratedFile.cpp
    src/Generation/GeneratedFile.h
    src/Generation/ReferenceWriter.cpp
    src/Generation/ReferenceWriter.h
    src/Generation/SdkPluginGenerator.cpp
    src/Generation/SdkPluginGenerator.h
    src/Generation/TypedMethodWriter.cpp
    src/Generation/TypedMethodWriter.h
    src/Generation/WrapperErrors.cpp
    src/Generation/WrapperErrors.h
    src/Manifest/ContractFields.cpp
    src/Manifest/ContractFields.h
    src/Manifest/ContractSchema.cpp
    src/Manifest/ContractSchema.h
    src/Manifest/ContractValidator.cpp
    src/Manifest/ContractValidator.h
    src/Manifest/JsonDocument.cpp
    src/Manifest/JsonDocument.h
    src/Manifest/Library.cpp
    src/Manifest/Library.h
    src/Manifest/LibraryCoverage.cpp
    src/Manifest/LibraryCoverage.h
    src/Manifest/PortableNames.cpp
    src/Manifest/PortableNames.h
    src/Manifest/SdkInputs.cpp
    src/Manifest/SdkInputs.h
    src/Manifest/SdkPlan.cpp
    src/Manifest/SdkPlan.h
    src/Process/Handle.cpp
    src/Process/Handle.h
    src/Process/ProcessArguments.cpp
    src/Process/ProcessArguments.h
    src/Process/ProcessCancellation.cpp
    src/Process/ProcessCancellation.h
    src/Process/ToolProcess.cpp
    src/Process/ToolProcess.h
)

set(development_editor_resources "${CMAKE_CURRENT_SOURCE_DIR}/Resources/DevelopmentEditor")
set(development_editor_file_list "${development_editor_resources}/Files.cmake")
set(embed_development_editor "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedDevelopmentEditor.cmake")
include("${development_editor_file_list}")
set(development_editor_inputs "")
foreach(resource IN LISTS development_editor_files)
    list(APPEND development_editor_inputs "${development_editor_resources}/${resource}")
endforeach()

set(development_editor_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/DevelopmentEditorResources.cpp")
add_custom_command(
    OUTPUT "${development_editor_cpp}"
    COMMAND "${CMAKE_COMMAND}"
        "-DRESOURCE_DIRECTORY=${development_editor_resources}"
        "-DOUTPUT_FILE=${development_editor_cpp}"
        -P "${embed_development_editor}"
    DEPENDS "${embed_development_editor}" "${development_editor_file_list}" ${development_editor_inputs}
    VERBATIM
)
target_sources(ModSDKTool PRIVATE "${development_editor_cpp}")

target_include_directories(ModSDKTool SYSTEM PRIVATE ../ThirdParty/nlohmann/include)

target_link_libraries(ModSDKTool PRIVATE ModToolSupport)

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
