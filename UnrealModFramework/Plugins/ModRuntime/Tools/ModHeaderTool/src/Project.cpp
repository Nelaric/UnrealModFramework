// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/Project.h"

#include "WatchedFiles.h"

#include <algorithm>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
namespace
{
void AddDirectory(FProject& Project, const fs::path& Path, bool bRequired)
{
	if (!fs::exists(Path))
	{
		if (bRequired)
		{
			throw std::runtime_error(PathToUtf8(Project.ConfigFile) +
			                         ": scan directory does not exist: " + PathToUtf8(Path));
		}
		return;
	}
	if (!fs::is_directory(Path))
	{
		throw std::runtime_error("Scan path is not a directory: " + PathToUtf8(Path));
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

void LoadDirectories(FProject& Project)
{
	using FJson = nlohmann::json;
	const std::string Contents = ReadTextFile(Project.ConfigFile);
	std::vector<std::set<std::string>> ObjectKeys;
	FJson Config;
	try
	{
		Config = FJson::parse(Contents,
		                      [&ObjectKeys](int, FJson::parse_event_t Event, FJson& Value)
		                      {
			                      if (Event == FJson::parse_event_t::object_start)
			                      {
				                      ObjectKeys.emplace_back();
			                      }
			                      else if (Event == FJson::parse_event_t::key)
			                      {
				                      const std::string Key = Value.get<std::string>();
				                      if (!ObjectKeys.back().insert(Key).second)
				                      {
					                      throw std::runtime_error("Duplicate JSON field '" + Key + "'.");
				                      }
			                      }
			                      else if (Event == FJson::parse_event_t::object_end)
			                      {
				                      ObjectKeys.pop_back();
			                      }
			                      return true;
		                      });
	}
	catch (const std::exception& Error)
	{
		throw std::runtime_error(PathToUtf8(Project.ConfigFile) + ": invalid JSON: " + Error.what());
	}
	if (!Config.is_object())
	{
		throw std::runtime_error(PathToUtf8(Project.ConfigFile) + ": expected a JSON object.");
	}
	for (const auto& Entry : Config.items())
	{
		if (Entry.key() != "Directories")
		{
			throw std::runtime_error(PathToUtf8(Project.ConfigFile) + ": unknown field '" + Entry.key() + "'.");
		}
	}
	if (!Config.contains("Directories"))
	{
		return;
	}
	if (!Config["Directories"].is_array())
	{
		throw std::runtime_error(PathToUtf8(Project.ConfigFile) + ": 'Directories' must be an array of strings.");
	}
	std::size_t Index = 0;
	for (const FJson& Value : Config["Directories"])
	{
		if (!Value.is_string() || Value.get_ref<const std::string&>().empty() ||
		    Value.get_ref<const std::string&>().find('\0') != std::string::npos)
		{
			throw std::runtime_error(PathToUtf8(Project.ConfigFile) + ": Directories[" + std::to_string(Index) +
			                         "] must be a nonempty path string without NUL characters.");
		}
		const fs::path Directory = PathFromUtf8(Value.get_ref<const std::string&>());
		AddDirectory(Project, Directory.is_absolute() ? Directory : Project.Root / Directory, true);
		++Index;
	}
}
} // namespace

FProject LoadProject(const fs::path& Input)
{
	fs::path ProjectFile = fs::absolute(Input).lexically_normal();
	if (fs::is_directory(ProjectFile))
	{
		std::vector<fs::path> Projects;
		for (const fs::directory_entry& Entry : fs::directory_iterator(ProjectFile))
		{
			if (Entry.is_regular_file() && Tools::LowercaseName(Entry.path().extension()) == u8".uproject")
			{
				Projects.push_back(Entry.path());
			}
		}
		if (Projects.size() != 1)
		{
			throw std::runtime_error("Expected exactly one .uproject in " + PathToUtf8(ProjectFile) +
			                         "; specify a project file with --project.");
		}
		ProjectFile = Projects.front();
	}
	if (!fs::is_regular_file(ProjectFile) || Tools::LowercaseName(ProjectFile.extension()) != u8".uproject")
	{
		throw std::runtime_error("Project must be an existing .uproject file: " + PathToUtf8(ProjectFile));
	}
	FProject Project;
	Project.File = fs::canonical(ProjectFile);
	Project.Root = Project.File.parent_path();
	Project.ConfigFile = Project.File;
	Project.ConfigFile.replace_extension(".modexpot");
	AddDirectory(Project, Project.Root / "Plugins", false);
	AddDirectory(Project, Project.Root / "Source", false);
	Project.bHasConfig = fs::exists(Project.ConfigFile);
	if (Project.bHasConfig)
	{
		LoadDirectories(Project);
	}
	return Project;
}

std::vector<fs::path> CollectSourceFiles(const FProject& Project)
{
	return Tools::CollectSourceFiles(Project.Directories);
}
} // namespace Mod::HeaderTool
