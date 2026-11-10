// Copyright (c) 2026 Nelaric Contributors
/** @file SourceAnalyzer.h */
#pragma once

#include "ModSourceTool/src/Analysis/SourceRequest.h"

#include <nlohmann/json.hpp>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class SourceAnalyzer
{
public:
	/// Analyzes escaping throws and configured failure returns in the Target.
	static Json AnalyzeErrors(const SourceRequest& Request);
};
} // namespace Mod::SourceTool
