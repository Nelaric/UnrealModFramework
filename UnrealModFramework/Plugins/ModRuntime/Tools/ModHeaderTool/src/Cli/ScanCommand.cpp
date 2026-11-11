// Copyright (c) 2026 Nelaric Contributors
#include "ScanCommand.h"

#include "ModHeaderTool/src/Scan/ProjectScan.h"
#include "ModHeaderTool/src/Scan/ScanReport.h"
#include "ModHeaderTool/src/Scan/ScanResult.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace Mod::HeaderTool
{
int ScanCommand::RunScan(const std::filesystem::path& Project, const std::filesystem::path& Snapshot)
{
	const ScanResult Result = ProjectScan::ScanProject(Project, Snapshot);
	if (!Result.Diagnostics.empty())
	{
		for (const std::string& Diagnostic : Result.Diagnostics)
		{
			std::cerr << Diagnostic << '\n';
		}
		return 1;
	}
	ScanReport::WriteScanReport(Result);
	return 0;
}
} // namespace Mod::HeaderTool
