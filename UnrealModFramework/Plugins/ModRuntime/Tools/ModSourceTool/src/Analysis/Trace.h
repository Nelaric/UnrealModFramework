// Copyright (c) 2026 Nelaric Contributors
/** @file Trace.h */
#pragma once

#include "ModSourceTool/src/Analysis/ArgumentValue.h"
#include "ModSourceTool/src/Analysis/Guard.h"

#include <clang-c/Index.h>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

struct Trace
{
	std::vector<std::string> ActiveFunctions;
	std::vector<CXCursor> Calls;
	std::vector<Guard> Guards;
	std::vector<ArgumentValue> Arguments;
	std::size_t FirstLocalGuard = 0;
	const Json* Caught = nullptr;
};
} // namespace Mod::SourceTool
