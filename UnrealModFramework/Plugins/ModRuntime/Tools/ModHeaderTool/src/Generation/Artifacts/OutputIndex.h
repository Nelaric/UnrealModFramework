// Copyright (c) 2026 Nelaric Contributors
/** @file OutputIndex.h */
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

struct OutputIndex
{
	Json Generated = Json::array();
	Json CompilationUnits = Json::array();
	std::string Hashes;
};
} // namespace Mod::HeaderTool
