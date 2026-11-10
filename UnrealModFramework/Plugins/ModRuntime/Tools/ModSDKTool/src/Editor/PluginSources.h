// Copyright (c) 2026 Nelaric Contributors
/** @file PluginSources.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class PluginSources
{
public:
	static void PrepareSdkSource(const SdkRequest& Request, const fs::path& Source);
	static void PrepareDevelopmentSource(const SdkRequest& Request, const fs::path& DevelopmentSource);
};
} // namespace Mod::SdkTool
