// Copyright (c) 2026 Nelaric Contributors
/** @file SdkPlan.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"
#include "ModSDKTool/src/Manifest/Library.h"

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
	static Json CollectLibraryPlans(SdkRequest& Request, const fs::path& Temporary);
	static std::string FingerprintPlan(const SdkRequest& Request, const Json& Jobs);
	static Json MakeSdkPlan(const SdkRequest& Request, const Json& Jobs);

private:
	static Json ReadLibraryPlan(const SdkRequest& Request, const Library& Job, const fs::path& ResultFile);
};
} // namespace Mod::SdkTool
