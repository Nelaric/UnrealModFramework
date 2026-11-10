// Copyright (c) 2026 Nelaric Contributors
/** @file PendingBindings.h */
#pragma once

#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

struct PendingBindings
{
	Json Failures = Json::array();
	std::set<std::pair<std::string, bool>> Unassigned;
};
} // namespace Mod::SourceTool
