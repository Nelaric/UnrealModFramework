// Copyright (c) 2026 Nelaric Contributors
/** @file AnalysisCommand.h */
#pragma once

#include "ModSourceTool/src/Cli/AnalysisOptions.h"

namespace Mod::SourceTool
{
class AnalysisCommand
{
public:
	static int RunAnalysis(AnalysisOptions Options, bool bUpdateCatalog);
};
} // namespace Mod::SourceTool
