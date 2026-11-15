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
	static constexpr char DevelopmentPluginName[] = "ModDevelopmentSDKEditor";
	static constexpr char DevelopmentModuleName[] = "DevelopmentEditor";

	static void PrepareSdkSource(const SdkRequest& Request, const fs::path& Source, bool bGameHost = false);
	/// Writes the embedded editor plugin into the build workspace and returns its descriptor.
	static fs::path PrepareDevelopmentSource(const fs::path& Work);
};
} // namespace Mod::SdkTool
