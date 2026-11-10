// Copyright (c) 2026 Nelaric Contributors
/** @file ArgumentValue.h */
#pragma once

#include <clang-c/Index.h>
#include <nlohmann/json.hpp>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

struct ArgumentValue
{
	CXCursor Parameter;
	Json Value;
};
} // namespace Mod::SourceTool
