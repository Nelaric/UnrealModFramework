// Copyright (c) 2026 Nelaric Contributors
/** @file SourceAnalyzer.h */
#pragma once

#include "ModSourceTool/src/Analysis/SourceRequest.h"

namespace Mod::SourceTool
{
class SourceAnalyzer
{
public:
	static Json AnalyzeErrors(const std::vector<SourceRequest>& Requests);
};
} // namespace Mod::SourceTool
