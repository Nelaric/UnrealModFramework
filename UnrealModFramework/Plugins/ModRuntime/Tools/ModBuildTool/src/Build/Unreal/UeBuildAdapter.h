// Copyright (c) 2026 Nelaric Contributors
/** @file UeBuildAdapter.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class UeBuildAdapter
{
public:
	/// Invalidates UE's UHT dependencies when a generation watch changes.
	static void RefreshUeWatches(const fs::path& ContextSet);
	/// Verifies and records the owning images after UE 5.6's link actions.
	static Json FinalizeUeTarget(const fs::path& ContextSet, const fs::path& TargetReceipt);
};
} // namespace Mod::BuildTool
