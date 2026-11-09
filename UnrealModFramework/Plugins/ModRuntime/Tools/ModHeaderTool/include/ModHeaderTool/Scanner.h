// Copyright (c) 2026 Nelaric Contributors
/** @file Scanner.h */
#pragma once

#include "ModHeaderTool/MacroScanner.h"
#include "ModHeaderTool/Project.h"

#include <vector>

namespace Mod::HeaderTool
{
/// A complete lexical scan, independent of its output format.
struct FScanResult
{
	/// Resolved project and scan configuration.
	FProject Project;
	/// Source files visited in deterministic order.
	std::vector<fs::path> SourceFiles;
	/// Macro invocations ordered by source file and byte offset.
	std::vector<FExportMacro> Exports;
	/// Option catalog used for parameter validation, if needed.
	fs::path MetadataOptions;
	/// Located errors that prevent publishing a successful scan report.
	std::vector<std::string> Diagnostics;
};

/// Loads the project and collects export macros from every source file.
FScanResult ScanProject(const fs::path& Input, const fs::path& Options = {});
} // namespace Mod::HeaderTool
