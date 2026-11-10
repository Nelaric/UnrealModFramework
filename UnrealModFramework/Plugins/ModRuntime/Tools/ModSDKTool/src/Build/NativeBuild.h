// Copyright (c) 2026 Nelaric Contributors
/** @file NativeBuild.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class NativeBuild
{
public:
	static Json BuildNativeLibraries(const SdkRequest& Request, const fs::path& Work);
};
} // namespace Mod::SdkTool
