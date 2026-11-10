// Copyright (c) 2026 Nelaric Contributors
/** @file BundlePublisher.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class BundlePublisher
{
public:
	static void Publish(const SdkRequest& Request, const fs::path& Stage);
};
} // namespace Mod::SdkTool
