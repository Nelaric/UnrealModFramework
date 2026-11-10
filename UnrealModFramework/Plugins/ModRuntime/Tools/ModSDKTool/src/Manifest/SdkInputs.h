// Copyright (c) 2026 Nelaric Contributors
/** @file SdkInputs.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class SdkInputs
{
public:
	static void AddInput(SdkRequest& Request, const fs::path& File);
};
} // namespace Mod::SdkTool
