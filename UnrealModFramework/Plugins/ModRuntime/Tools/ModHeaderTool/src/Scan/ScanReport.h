// Copyright (c) 2026 Nelaric Contributors
/** @file ScanReport.h */
#pragma once

#include "ModHeaderTool/src/Scan/ScanResult.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ScanReport
{
public:
	/// Writes the JSON scan report to a file or stdout for an empty output path.
	static void WriteScanReport(const ScanResult& Result, const fs::path& Output);
};
} // namespace Mod::HeaderTool
