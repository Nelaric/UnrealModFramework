// Copyright (c) 2026 Nelaric Contributors
/** @file UeTargetReceipt.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class UeTargetReceipt
{
public:
	static void RequireUeBinary(const Request& Request, const Json& TargetReceipt, const fs::path& ProjectRoot);

private:
	static fs::path UeProductPath(const fs::path& ProjectRoot, std::string_view Path);
};
} // namespace Mod::BuildTool
