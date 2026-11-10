// Copyright (c) 2026 Nelaric Contributors
/** @file SdkBundle.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class SdkBundle
{
public:
	/// Verifies an existing bundle's complete declared file inventory.
	static Json InspectSdk(const fs::path& Directory);
};
} // namespace Mod::SdkTool
