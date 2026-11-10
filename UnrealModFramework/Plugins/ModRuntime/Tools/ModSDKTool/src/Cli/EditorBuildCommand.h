// Copyright (c) 2026 Nelaric Contributors
/** @file EditorBuildCommand.h */
#pragma once

#include <map>
#include <string>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

class EditorBuildCommand
{
public:
	static int BuildEditorCommand(const Options& Options);
};
} // namespace Mod::SdkTool
