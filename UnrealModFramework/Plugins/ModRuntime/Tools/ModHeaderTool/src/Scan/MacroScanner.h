// Copyright (c) 2026 Nelaric Contributors
/** @file MacroScanner.h */
#pragma once

#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class MacroScanner
{
public:
	/// Collects invocations without evaluating conditional compilation.
	static std::vector<ExportMacro> ScanFile(const fs::path& Path);
	/// Collects invocations from an existing UTF-8 source buffer.
	static std::vector<ExportMacro> ScanSource(const fs::path& Path, const std::string& Source);
};
} // namespace Mod::HeaderTool
