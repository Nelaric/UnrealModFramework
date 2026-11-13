// Copyright (c) 2026 Nelaric Contributors
/** @file EditorPlugin.h */
#pragma once

#include "ModSDKTool/src/Editor/EditorBuild.h"

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class EditorPlugin
{
public:
	/// Builds and publishes the independent authoring plugin.
	static void BuildEditorPlugin(const EditorBuild& Editor, const fs::path& Package);
};
} // namespace Mod::SdkTool
