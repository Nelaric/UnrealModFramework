// Copyright (c) 2026 Nelaric Contributors
/** @file CompilerDependencies.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class CompilerDependencies
{
public:
	static std::vector<fs::path> ReadCompilerDependencies(const Request& Request, const fs::path& Path);
	static std::map<fs::path, std::string> HashCompilerDependencies(const std::set<fs::path>& CompilerDependencies,
	                                                                fs::file_time_type Started);
};
} // namespace Mod::BuildTool
