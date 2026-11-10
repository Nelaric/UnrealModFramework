// Copyright (c) 2026 Nelaric Contributors
/** @file ScanResult.h */
#pragma once

#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

/// A complete lexical scan, independent of its output format.
struct ScanResult
{
	/// Resolved project and scan configuration.
	Mod::HeaderTool::Project Project;
	/// Source files visited in deterministic order.
	std::vector<fs::path> SourceFiles;
	/// Macro invocations ordered by source file and byte offset.
	std::vector<ExportMacro> Exports;
	/// Option catalog used for parameter validation, if needed.
	fs::path MetadataOptions;
	/// Located errors that prevent publishing a successful scan report.
	std::vector<std::string> Diagnostics;
};
} // namespace Mod::HeaderTool
