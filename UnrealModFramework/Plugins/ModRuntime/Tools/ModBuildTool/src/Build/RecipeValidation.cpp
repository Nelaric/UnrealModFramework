// Copyright (c) 2026 Nelaric Contributors
#include "RecipeValidation.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Build/BuildInputs.h"
#include "ModBuildTool/src/Build/ContextDocument.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

bool RecipeValidation::ContainsMarker(const Json& Arguments, std::string_view Marker)
{
	return std::any_of(Arguments.begin(), Arguments.end(), [Marker](const Json& Value)
	                   { return Value.get<std::string>().find(Marker) != std::string::npos; });
}

void RecipeValidation::ValidateRecipe(Request& Request, std::string_view Name)
{
	Json& Recipe = Request.Context.at(std::string(Name));
	const fs::path Tool = Path::ResolvePath(Request.ContextPath.parent_path(), ContextDocument::Text(Recipe, "path"));
	Recipe["path"] = Path::PathToUtf8(Tool);
	BuildInputs::AddInput(Request, Tool);
	const std::string Format = ContextDocument::Text(Recipe, "responseFormat");
	if (Format != "msvc" && Format != "gnu")
	{
		throw std::runtime_error("Unsupported tool responseFormat: " + Format);
	}
	const Json& Arguments = Recipe.at("arguments");
	if (!Arguments.is_array() || Arguments.empty())
	{
		throw std::runtime_error("Tool arguments must be a nonempty argument array.");
	}
	for (const Json& Argument : Arguments)
	{
		const std::string Value = Argument.get<std::string>();
		if (Value.find_first_of("\r\n") != std::string::npos || Value.find('\0') != std::string::npos)
		{
			throw std::runtime_error("Tool arguments cannot contain NUL or newlines.");
		}
	}
	if (Name == "compiler")
	{
		if (!ContainsMarker(Arguments, "{source}") || !ContainsMarker(Arguments, "{object}") ||
		    !ContainsMarker(Arguments, "{dependencies}"))
		{
			throw std::runtime_error("Compiler arguments must name {source}, {object}, and {dependencies}.");
		}
		const std::string Dependencies = ContextDocument::Text(Recipe, "dependencyFormat");
		if (Dependencies != "make" && Dependencies != "msvc-json")
		{
			throw std::runtime_error("Unsupported compiler dependencyFormat.");
		}
	}
	else if (!ContainsMarker(Arguments, "{output}") ||
	         std::find(Arguments.begin(), Arguments.end(), Json("{objects}")) == Arguments.end())
	{
		throw std::runtime_error("Archiver arguments require {output} and a standalone {objects} argument.");
	}
}
} // namespace Mod::BuildTool
