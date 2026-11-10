// Copyright (c) 2026 Nelaric Contributors
/** @file RecipeValidation.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <nlohmann/json.hpp>
#include <string_view>

namespace Mod::BuildTool
{
using Json = nlohmann::json;

class RecipeValidation
{
public:
	static void ValidateRecipe(Request& Request, std::string_view Name);

private:
	static bool ContainsMarker(const Json& Arguments, std::string_view Marker);
};
} // namespace Mod::BuildTool
