// Copyright (c) 2026 Nelaric Contributors
/** @file EditorPackage.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"
#include "ModSDKTool/src/Editor/EditorBuild.h"
#include "ModSDKTool/src/Editor/PluginPackages.h"

#include <filesystem>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class EditorPackage
{
public:
	static void BuildDevelopmentPlugin(const EditorBuild& Editor, const fs::path& Plugin, const fs::path& Package);
	static PluginPackages BuildSdkPackages(const SdkRequest& Request, const fs::path& Work, const std::string& Plugin);

private:
	static void BuildPlugin(const EditorBuild& Editor, const fs::path& Plugin, const fs::path& Package);
	static void VerifyEditorModule(const EditorBuild& Editor, const fs::path& Package, const std::string& Module);
};
} // namespace Mod::SdkTool
