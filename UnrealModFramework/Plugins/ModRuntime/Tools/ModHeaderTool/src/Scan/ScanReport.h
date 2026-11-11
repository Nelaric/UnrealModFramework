// Copyright (c) 2026 Nelaric Contributors
/** @file ScanReport.h */
#pragma once

#include "ModHeaderTool/src/Scan/ScanResult.h"

namespace Mod::HeaderTool
{
class ScanReport
{
public:
	/// Writes the JSON scan report to stdout.
	static void WriteScanReport(const ScanResult& Result);
};
} // namespace Mod::HeaderTool
