// Copyright (c) 2026 Nelaric Contributors
#include "ScanCommand.h"

#include "ModHeaderTool/src/Scan/ProjectScan.h"
#include "ModHeaderTool/src/Scan/ScanReport.h"
#include "ModHeaderTool/src/Scan/ScanResult.h"

#include <iostream>
#include <string>

namespace Mod::HeaderTool
{
int ScanCommand::RunScan(const CommandLineOptions& Options)
{
	const ScanResult Result = ProjectScan::ScanProject(Options.Project, Options.Options);
	if (!Result.Diagnostics.empty())
	{
		for (const std::string& Diagnostic : Result.Diagnostics)
		{
			std::cerr << Diagnostic << '\n';
		}
		return 1;
	}
	ScanReport::WriteScanReport(Result, Options.Output);
	return 0;
}
} // namespace Mod::HeaderTool
