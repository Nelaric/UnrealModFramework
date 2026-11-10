// Copyright (c) 2026 Nelaric Contributors
/** @file HostPlan.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <nlohmann/json.hpp>

namespace Mod::BuildTool
{
using Json = nlohmann::json;

class HostPlan
{
public:
	static Json MakePlan(const Request& Request);
	static Json CurrentHostPlan(const Request& Request);
};
} // namespace Mod::BuildTool
