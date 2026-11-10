// Copyright (c) 2026 Nelaric Contributors
/** @file EnvironmentBlock.h */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
#ifdef _WIN32
class EnvironmentBlock
{
public:
	static std::vector<wchar_t> MakeEnvironment(const std::map<std::string, std::string>& Overrides);
};
#endif
} // namespace Mod::BuildTool
