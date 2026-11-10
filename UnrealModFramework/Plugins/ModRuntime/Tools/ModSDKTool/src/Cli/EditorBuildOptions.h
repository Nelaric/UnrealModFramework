// Copyright (c) 2026 Nelaric Contributors
/** @file EditorBuildOptions.h */
#pragma once

#include "ModSDKTool/src/Editor/EditorBuild.h"

#include <map>
#include <string>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

class EditorBuildOptions
{
public:
	static EditorBuild ReadEditorBuild(const Options& Options);
};
} // namespace Mod::SdkTool
