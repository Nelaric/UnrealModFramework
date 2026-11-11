// Copyright (c) 2026 Nelaric Contributors
/** @file Process.h */
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class Process
{
public:
	/// Runs an argument vector and optionally captures stdout in a fresh file.
	static int RunProcess(const fs::path& Executable, const std::vector<std::string>& Arguments,
	                      const fs::path& WorkingDirectory, const std::map<std::string, std::string>& Environment,
	                      const fs::path& StandardOutput = {});
};
} // namespace Mod::BuildTool
