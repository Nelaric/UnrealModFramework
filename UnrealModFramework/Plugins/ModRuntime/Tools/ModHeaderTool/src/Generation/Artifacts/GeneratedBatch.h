// Copyright (c) 2026 Nelaric Contributors
/** @file GeneratedBatch.h */
#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;
using Files = std::map<std::string, std::string>;

struct GeneratedBatch
{
	Mod::HeaderTool::Files Files;
	Json NativeABI;
	std::string ErrorHeader;
	std::string ContractHash;
	std::string ProjectionHash;
};
} // namespace Mod::HeaderTool
