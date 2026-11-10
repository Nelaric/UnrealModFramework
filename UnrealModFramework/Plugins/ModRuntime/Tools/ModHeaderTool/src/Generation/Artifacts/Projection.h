// Copyright (c) 2026 Nelaric Contributors
/** @file Projection.h */
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

struct Projection
{
	Json Contract;
	Json Sources = Json::array();
	Json Lifecycle;
	std::string LifecycleExport;
	std::string ErrorHeader;
	std::string ContractHash;
	std::string ProjectionHash;
};
} // namespace Mod::HeaderTool
