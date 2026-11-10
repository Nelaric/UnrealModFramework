// Copyright (c) 2026 Nelaric Contributors
/** @file BundleInventory.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class BundleInventory
{
public:
	static Json Inventory(const fs::path& Root);
};
} // namespace Mod::SdkTool
