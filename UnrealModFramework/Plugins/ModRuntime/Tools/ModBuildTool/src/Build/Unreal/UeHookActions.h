// Copyright (c) 2026 Nelaric Contributors
/** @file UeHookActions.h */
#pragma once

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

struct UeHookSource
{
	fs::path Copy;
	std::string GeneratedHash;
	bool operator==(const UeHookSource&) const = default;
};

using UeHookSources = std::map<fs::path, UeHookSource>;
using UeHookObjects = std::map<fs::path, fs::path>;

class UeHookActions
{
public:
	/// Restores exported dependencies and substitutes generated compile inputs.
	static UeHookObjects Prepare(nlohmann::json& Graph, const UeHookSources& Sources, const fs::path& Stage);
};
} // namespace Mod::BuildTool
