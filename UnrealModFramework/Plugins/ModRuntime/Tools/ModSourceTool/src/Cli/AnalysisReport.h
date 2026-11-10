// Copyright (c) 2026 Nelaric Contributors
/** @file AnalysisReport.h */
#pragma once

#include "ModSourceTool/src/Analysis/SourceRequest.h"
#include "ModSourceTool/src/Errors/ErrorCatalog.h"

#include <nlohmann/json.hpp>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class AnalysisReport
{
public:
	static Json AnalyzeReport(ErrorCatalog& Catalog, const SourceRequest& Request);
};
} // namespace Mod::SourceTool
