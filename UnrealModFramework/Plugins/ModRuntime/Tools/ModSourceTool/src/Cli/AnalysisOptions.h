// Copyright (c) 2026 Nelaric Contributors
/** @file AnalysisOptions.h */
#pragma once

#include "ModSourceTool/src/Analysis/SourceRequest.h"

namespace Mod::SourceTool
{
struct AnalysisOptions
{
	std::vector<SourceRequest> Requests;

	static AnalysisOptions Read(const std::filesystem::path& File);
};
} // namespace Mod::SourceTool
