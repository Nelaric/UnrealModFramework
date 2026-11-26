// Copyright (c) 2026 Nelaric Contributors
/** @file SdkPlan.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class SdkPlan
{
public:
	static std::string FingerprintPlan(const SdkRequest& Request);
	static Json MakeSdkPlan(const SdkRequest& Request);
};
} // namespace Mod::SdkTool
