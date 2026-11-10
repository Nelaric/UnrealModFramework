// Copyright (c) 2026 Nelaric Contributors
/** @file ToolProcess.h */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class ToolProcess
{
public:
	/// Runs a native child with a deadline and process-tree cancellation.
	static int RunTool(const fs::path& Executable, const std::vector<std::string>& Arguments,
	                   const fs::path& WorkingDirectory, unsigned TimeoutSeconds);
};
} // namespace Mod::SdkTool
