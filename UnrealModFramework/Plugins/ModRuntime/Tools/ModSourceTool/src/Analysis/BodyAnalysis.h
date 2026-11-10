// Copyright (c) 2026 Nelaric Contributors
/** @file BodyAnalysis.h */
#pragma once

#include <clang-c/Index.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

struct BodyAnalysis
{
	CXCursor Body;
	CXCursor Definition;
	Json Failures = Json::array();
	std::vector<CXCursor> NonNullSkeletons{};
	std::optional<Json> FailureReturn;
	bool bTraceThrows = false;
	Json& Diagnostics;
};
} // namespace Mod::SourceTool
