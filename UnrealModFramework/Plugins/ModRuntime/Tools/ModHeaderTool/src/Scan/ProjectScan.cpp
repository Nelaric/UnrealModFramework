// Copyright (c) 2026 Nelaric Contributors
#include "ProjectScan.h"

#include "ModHeaderTool/src/Scan/ProjectScanner.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

ScanResult ProjectScan::ScanProject(const fs::path& Input, const fs::path& OptionsFile)
{
	return ProjectScanner(Input, OptionsFile).Run();
}
} // namespace Mod::HeaderTool
