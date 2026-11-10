// Copyright (c) 2026 Nelaric Contributors
/** @file SdkPluginGenerator.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class SdkPluginGenerator
{
public:
	/// Generates UE SDK source from supported, already resolved projections.
	static void GenerateSdkPlugin(const SdkRequest& Request, const fs::path& Directory);
};
} // namespace Mod::SdkTool
