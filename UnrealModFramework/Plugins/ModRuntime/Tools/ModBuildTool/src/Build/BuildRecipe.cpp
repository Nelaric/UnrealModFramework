// Copyright (c) 2026 Nelaric Contributors
#include "BuildRecipe.h"

#include "Common/Files/Path.h"
#include "Common/Process/Process.h"
#include "Common/Process/ResponseFile.h"
#include "ModBuildTool/src/Build/ContextDocument.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::vector<std::string> BuildRecipe::ExpandArguments(const Json& Recipe,
                                                      const std::map<std::string, std::string>& Values,
                                                      const std::vector<fs::path>& Objects)
{
	std::vector<std::string> Result;
	for (const Json& Item : Recipe.at("arguments"))
	{
		std::string Argument = Item.get<std::string>();
		if (Argument == "{objects}")
		{
			for (const fs::path& Object : Objects)
			{
				Result.push_back(Path::PathToUtf8(Object));
			}
			continue;
		}
		for (const auto& [Marker, Value] : Values)
		{
			std::size_t Position = 0;
			while ((Position = Argument.find(Marker, Position)) != std::string::npos)
			{
				Argument.replace(Position, Marker.size(), Value);
				Position += Value.size();
			}
		}
		Result.push_back(std::move(Argument));
	}
	return Result;
}

void BuildRecipe::ExecuteRecipe(const Request& Request, std::string_view Name, const fs::path& Response,
                                const std::map<std::string, std::string>& Values, const std::vector<fs::path>& Objects,
                                const std::string& LinkWitness)
{
	const Json& Recipe = Request.Context.at(std::string(Name));
	auto Arguments = ExpandArguments(Recipe, Values, Objects);
	if (!LinkWitness.empty())
	{
		Arguments.push_back("/DUMF_MOD_HOST_LINK_WITNESS=" + LinkWitness);
	}
	ResponseFile::WriteResponseFile(Response, Arguments, ContextDocument::Text(Recipe, "responseFormat"));
	const int Code =
	    Process::RunProcess(Path::PathFromUtf8(ContextDocument::Text(Recipe, "path")),
	                        {"@" + Path::PathToUtf8(Response)}, Request.WorkingDirectory, Request.Environment);
	if (Code != 0)
	{
		throw std::runtime_error(std::string(Name) + " failed with exit code " + std::to_string(Code) + ".");
	}
}
} // namespace Mod::BuildTool
