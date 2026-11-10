// Copyright (c) 2026 Nelaric Contributors
/** @file ProjectScanner.h */
#pragma once

#include "ModHeaderTool/src/Metadata/MetadataOptions.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"
#include "ModHeaderTool/src/Scan/ScanResult.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ProjectScanner
{
public:
	ProjectScanner(const fs::path& Input, const fs::path& OptionsFile);
	ScanResult Run() &&;

private:
	void LoadOptions();
	void ScanFile(const fs::path& SourceFile);
	void ValidateExport(ExportMacro& Macro, const std::string& Source);

	ScanResult Result;
	fs::path CatalogPath;
	std::optional<MetadataOptions> Options;
	bool bAttemptedOptions = false;
};
} // namespace Mod::HeaderTool
