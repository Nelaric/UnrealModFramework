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
	/// Executes an argument vector without a shell; child output goes to stderr.
	static int RunProcess(const fs::path& Executable, const std::vector<std::string>& Arguments,
	                      const fs::path& WorkingDirectory, const std::map<std::string, std::string>& Environment);
};
} // namespace Mod::BuildTool
