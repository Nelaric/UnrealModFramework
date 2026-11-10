// Copyright (c) 2026 Nelaric Contributors
/** @file CommandArguments.h */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
struct CommandArguments
{
	const std::string& Name;
	std::map<std::string, std::string> Options;
	std::vector<std::string> Symbols;
};
} // namespace Mod::BuildTool
