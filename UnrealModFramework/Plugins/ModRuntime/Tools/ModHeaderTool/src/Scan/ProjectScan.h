// Copyright (c) 2026 Nelaric Contributors
/** @file ProjectScan.h */
#pragma once

#include "ModHeaderTool/src/Scan/ScanResult.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ProjectScan
{
public:
	/// Loads the project and collects export macros from every source file.
	static ScanResult ScanProject(const fs::path& Input, const fs::path& Options = {});
};
} // namespace Mod::HeaderTool
