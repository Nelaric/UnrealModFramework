// Copyright (c) 2026 Nelaric Contributors
/** @file BundleAssembly.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"
#include "ModSDKTool/src/Editor/PluginPackages.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class BundleAssembly
{
public:
	static void CopySdkPackages(const SdkRequest& Request, const PluginPackages& Packages, const std::string& Plugin,
	                            const fs::path& Bundle);
	static void CopySdkNativeArtifacts(const SdkRequest& Request, const fs::path& Bundle);
	static Json WriteBundleManifest(const SdkRequest& Request, const fs::path& Bundle, const std::string& Plugin);
};
} // namespace Mod::SdkTool
