# Copyright (c) 2026 Nelaric Contributors

# Paths are relative to the plugin root written by MDT.
set(development_editor_files
    README.md
    README.zh-CN.md
    Source/DevelopmentEditor/DevelopmentEditor.Build.cs
    Source/DevelopmentEditor/Private/DevelopmentEditorModule.cpp
    Source/DevelopmentEditor/Private/ModPackageCommandlet.cpp
    Source/DevelopmentEditor/Private/ModPackageCommandlet.h
    Source/DevelopmentEditor/Private/ModPackageEditor.cpp
    Source/DevelopmentEditor/Private/ModPackageEditor.h
    Source/DevelopmentEditor/Private/ModPackagingSettings.cpp
    Source/DevelopmentEditor/Private/ModPackagingSettings.h
    Source/DevelopmentEditor/Private/ModSdkBuildEditor.cpp
    Source/DevelopmentEditor/Private/ModSdkBuildEditor.h
    Source/DevelopmentEditor/Private/Packaging/ModNativeTarget.h
    Source/DevelopmentEditor/Private/Packaging/ModPackageBuilder.cpp
    Source/DevelopmentEditor/Private/Packaging/ModPackageBuilder.h
    Source/DevelopmentEditor/Private/Packaging/ModProjectPackage.cpp
    Source/DevelopmentEditor/Private/Packaging/ModProjectPackage.h
)

# Embed the same target rules used by the game into the standalone authoring plugin.
function(development_editor_source_path resource output)
    if(resource STREQUAL "Source/DevelopmentEditor/Private/Packaging/ModNativeTarget.h")
        set(source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../../Source/Runtime/ModLoader/Public/ModNativeTarget.h")
    else()
        set(source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${resource}")
    endif()
    set("${output}" "${source}" PARENT_SCOPE)
endfunction()
