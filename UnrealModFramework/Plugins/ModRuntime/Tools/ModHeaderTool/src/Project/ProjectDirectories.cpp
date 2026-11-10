// Copyright (c) 2026 Nelaric Contributors
#include "ProjectDirectories.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void ProjectDirectories::AddDirectory(Project& Project, const fs::path& Path, bool bRequired)
{
	if (!fs::exists(Path))
	{
		if (bRequired)
		{
			throw std::runtime_error(
			    DiagnosticLocation::QuoteDiagnosticPath(Project.ConfigFile) +
			    ": scan directory does not exist: " + DiagnosticLocation::QuoteDiagnosticPath(Path));
		}
		return;
	}
	if (!fs::is_directory(Path))
	{
		throw std::runtime_error("Scan path is not a directory: " + DiagnosticLocation::QuoteDiagnosticPath(Path));
	}
	const fs::path CanonicalPath = fs::canonical(Path);
	for (const fs::path& Existing : Project.Directories)
	{
		if (fs::equivalent(Existing, CanonicalPath))
		{
			return;
		}
	}
	Project.Directories.push_back(CanonicalPath);
}

void ProjectDirectories::LoadDirectories(Project& Project)
{
	using Json = nlohmann::json;
	const std::string Contents = TextFile::ReadTextFile(Project.ConfigFile);
	std::vector<std::set<std::string>> ObjectKeys;
	Json Config;
	try
	{
		Config = Json::parse(Contents,
		                     [&ObjectKeys](int, Json::parse_event_t Event, Json& Value)
		                     {
			                     if (Event == Json::parse_event_t::object_start)
			                     {
				                     ObjectKeys.emplace_back();
			                     }
			                     else if (Event == Json::parse_event_t::key)
			                     {
				                     const std::string Key = Value.get<std::string>();
				                     if (!ObjectKeys.back().insert(Key).second)
				                     {
					                     throw std::runtime_error("Duplicate JSON field '" + Key + "'.");
				                     }
			                     }
			                     else if (Event == Json::parse_event_t::object_end)
			                     {
				                     ObjectKeys.pop_back();
			                     }
			                     return true;
		                     });
	}
	catch (const std::exception& Error)
	{
		throw std::runtime_error(DiagnosticLocation::QuoteDiagnosticPath(Project.ConfigFile) +
		                         ": invalid JSON: " + Error.what());
	}
	if (!Config.is_object())
	{
		throw std::runtime_error(DiagnosticLocation::QuoteDiagnosticPath(Project.ConfigFile) +
		                         ": expected a JSON object.");
	}
	for (const auto& Entry : Config.items())
	{
		if (Entry.key() != "Directories")
		{
			throw std::runtime_error(DiagnosticLocation::QuoteDiagnosticPath(Project.ConfigFile) + ": unknown field '" +
			                         Entry.key() + "'.");
		}
	}
	if (!Config.contains("Directories"))
	{
		return;
	}
	if (!Config["Directories"].is_array())
	{
		throw std::runtime_error(DiagnosticLocation::QuoteDiagnosticPath(Project.ConfigFile) +
		                         ": 'Directories' must be an array of strings.");
	}
	std::size_t Index = 0;
	for (const Json& Value : Config["Directories"])
	{
		if (!Value.is_string() || Value.get_ref<const std::string&>().empty() ||
		    Value.get_ref<const std::string&>().find('\0') != std::string::npos)
		{
			throw std::runtime_error(DiagnosticLocation::QuoteDiagnosticPath(Project.ConfigFile) + ": Directories[" +
			                         std::to_string(Index) +
			                         "] must be a nonempty path string without NUL characters.");
		}
		const fs::path Directory = Path::PathFromUtf8(Value.get_ref<const std::string&>());
		AddDirectory(Project, Directory.is_absolute() ? Directory : Project.Root / Directory, true);
		++Index;
	}
}
} // namespace Mod::HeaderTool
