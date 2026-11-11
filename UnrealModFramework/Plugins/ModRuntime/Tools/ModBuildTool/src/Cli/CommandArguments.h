// Copyright (c) 2026 Nelaric Contributors
/** @file CommandArguments.h */
#pragma once

#include <map>
#include <string>

namespace Mod::BuildTool
{
struct CommandArguments
{
	const std::string& Name;
	std::map<std::string, std::string> Options;
};
} // namespace Mod::BuildTool
