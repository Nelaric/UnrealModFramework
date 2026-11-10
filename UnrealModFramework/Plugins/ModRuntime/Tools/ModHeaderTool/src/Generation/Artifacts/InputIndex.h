// Copyright (c) 2026 Nelaric Contributors
/** @file InputIndex.h */
#pragma once

#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

struct InputIndex
{
	Json Dependencies = Json::array();
	Json Files = Json::array();
};
} // namespace Mod::HeaderTool
