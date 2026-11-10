// Copyright (c) 2026 Nelaric Contributors
/** @file BuildInputs.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class BuildInputs
{
public:
	static void AddInput(Request& Request, const fs::path& Path);
	static fs::path ChangedWatchedDirectory(const Json& Directories);
	static Request ReadRequest(const fs::path& ContextPath, const fs::path& ManifestPath);
	static Json InputList(const std::map<fs::path, std::string>& Inputs);
	static std::string Fingerprint(const std::map<fs::path, std::string>& Inputs);

private:
	static void ValidateGenerationInputs(Request& Request);
};
} // namespace Mod::BuildTool
