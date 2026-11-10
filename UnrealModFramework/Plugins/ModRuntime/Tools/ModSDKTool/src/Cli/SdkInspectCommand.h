// Copyright (c) 2026 Nelaric Contributors
/** @file SdkInspectCommand.h */
#pragma once

#include <map>
#include <string>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

class SdkInspectCommand
{
public:
	static int InspectSdkCommand(const Options& Options);
};
} // namespace Mod::SdkTool
