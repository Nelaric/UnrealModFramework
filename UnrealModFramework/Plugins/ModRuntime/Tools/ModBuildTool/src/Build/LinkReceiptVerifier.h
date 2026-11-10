// Copyright (c) 2026 Nelaric Contributors
/** @file LinkReceiptVerifier.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class LinkReceiptVerifier
{
public:
	static Json VerifyLink(const Request& Request, const Json& Plan, const fs::path& LinkPath, const Json& Record,
	                       const std::string& LinkHash);
};
} // namespace Mod::BuildTool
