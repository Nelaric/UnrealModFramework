// Copyright (c) 2026 Nelaric Contributors
/** @file ScanReport.h */
#pragma once

#include "ModHeaderTool/Scanner.h"

namespace Mod::HeaderTool
{
/// Writes the JSON scan report to a file or stdout for an empty output path.
void WriteScanReport(const FScanResult& Result, const fs::path& Output);
} // namespace Mod::HeaderTool
