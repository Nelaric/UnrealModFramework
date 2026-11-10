// Copyright (c) 2026 Nelaric Contributors
/** @file HostCommandResult.h */
#pragma once

#include "ModBuildTool/src/Build/HostBuild.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class HostCommandResult
{
public:
	static Json ExecuteHostAction(const HostBuild& Host, std::string_view Name, std::string_view Expected,
	                              const fs::path& LinkReceipt);
	static void WriteHostResult(const HostBuild& Host, const fs::path& Output, const fs::path& LinkReceipt,
	                            const Json& Result);
};
} // namespace Mod::BuildTool
