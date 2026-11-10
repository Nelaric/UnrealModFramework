// Copyright (c) 2026 Nelaric Contributors
/** @file BuildRecipe.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class BuildRecipe
{
public:
	static void ExecuteRecipe(const Request& Request, std::string_view Name, const fs::path& Response,
	                          const std::map<std::string, std::string>& Values,
	                          const std::vector<fs::path>& Objects = {}, const std::string& LinkWitness = {});

private:
	static std::vector<std::string> ExpandArguments(const Json& Recipe,
	                                                const std::map<std::string, std::string>& Values,
	                                                const std::vector<fs::path>& Objects = {});
};
} // namespace Mod::BuildTool
