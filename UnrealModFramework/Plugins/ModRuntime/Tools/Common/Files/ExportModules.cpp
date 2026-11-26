// Copyright (c) 2026 Nelaric Contributors
#include "ExportModules.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/PathName.h"
#include "Common/Files/WatchedFiles.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>

namespace Mod::Tools
{
namespace fs = std::filesystem;
using BuildTool::Path;
using Json = nlohmann::json;

namespace
{
using ModuleNames = std::set<std::u8string>;

const std::string& ReadText(const Json& Value, const std::string& Field)
{
	const auto* Text = Value.get_ptr<const std::string*>();
	if (!Text || Text->empty() || Text->find('\0') != std::string::npos)
	{
		throw std::runtime_error(Field + " requires a nonempty string.");
	}
	return *Text;
}

ModuleNames ReadExclusions(const Json& Config, const std::string& Field)
{
	ModuleNames Names;
	if (const auto Values = Config.find("ExcludeModules"); Values != Config.end())
	{
		if (!Values->is_array())
		{
			throw std::runtime_error(Field + " must be an array of module names.");
		}
		for (const auto& Value : *Values)
		{
			Names.insert(PathName::LowercaseName(Path::PathFromUtf8(ReadText(Value, Field))));
		}
	}
	return Names;
}

// Like UBT, stop at a descriptor or module rules file instead of descending into its implementation.
std::vector<fs::path> FindRoots(const fs::path& Root, std::u8string_view Suffix)
{
	std::vector<fs::path> Matches, Pending{Root};
	std::set<fs::path> Visited;
	while (!Pending.empty())
	{
		const auto Directory = Pending.back();
		Pending.pop_back();
		if (!fs::is_directory(Directory) || !Visited.insert(fs::canonical(Directory)).second)
		{
			continue;
		}
		std::vector<fs::path> Children;
		bool Found = false;
		for (const auto& Entry : fs::directory_iterator(Directory))
		{
			if (Entry.is_regular_file() && PathName::LowercaseName(Entry.path().filename()).ends_with(Suffix))
			{
				Found = true;
				Matches.push_back(fs::canonical(Entry.path()));
			}
			else if (Entry.is_directory() && !WatchedFiles::IsExcludedDirectory(Entry.path()))
			{
				Children.push_back(Entry.path());
			}
		}
		if (!Found)
		{
			Pending.insert(Pending.end(), Children.begin(), Children.end());
		}
	}
	std::sort(Matches.begin(), Matches.end());
	return Matches;
}

fs::path ResolvePlugin(const fs::path& Root, const std::string& Reference,
                       std::optional<std::vector<fs::path>>& Available)
{
	const auto Plugin = Path::PathFromUtf8(Reference);
	if (PathName::LowercaseName(Plugin.extension()) == u8".uplugin")
	{
		const auto Descriptor = Root / Plugin;
		if (!fs::is_regular_file(Descriptor))
		{
			throw std::runtime_error("Plugin descriptor does not exist: " + Reference);
		}
		return fs::canonical(Descriptor);
	}
	if (!Available)
	{
		Available = FindRoots(Root / "Plugins", u8".uplugin");
	}
	const auto Name = PathName::LowercaseName(Plugin);
	std::vector<fs::path> Matches;
	for (const auto& Candidate : *Available)
	{
		if (PathName::LowercaseName(Candidate.stem()) == Name)
		{
			Matches.push_back(Candidate);
		}
	}
	if (Matches.size() != 1)
	{
		throw std::runtime_error("Expected one project plugin named '" + Reference + "'. Supply its .uplugin path.");
	}
	return Matches.front();
}

std::map<fs::path, ModuleNames> ReadPlugins(const fs::path& Root, const Json& Plugins)
{
	if (!Plugins.is_array())
	{
		throw std::runtime_error("Plugins must be an array of plugin names, .uplugin paths, or objects.");
	}
	std::map<fs::path, ModuleNames> Selected;
	std::optional<std::vector<fs::path>> Available;
	for (std::size_t Index = 0; Index < Plugins.size(); ++Index)
	{
		const auto& Entry = Plugins[Index];
		const auto Field = "Plugins[" + std::to_string(Index) + "]";
		if (Entry.is_string())
		{
			Selected.try_emplace(ResolvePlugin(Root, ReadText(Entry, Field), Available));
			continue;
		}
		if (!Entry.is_object() || Entry.contains("Name") == Entry.contains("Path"))
		{
			throw std::runtime_error(Field + " requires one plugin name or .uplugin path, using either Name or Path.");
		}
		for (const auto& Key : Entry.items())
		{
			if (Key.key() != "Name" && Key.key() != "Path" && Key.key() != "ExcludeModules")
			{
				throw std::runtime_error("Unknown field '" + Field + "." + Key.key() + "'.");
			}
		}
		const bool ByPath = Entry.contains("Path");
		const auto& Reference = ReadText(Entry.at(ByPath ? "Path" : "Name"), Field + (ByPath ? ".Path" : ".Name"));
		if (ByPath && PathName::LowercaseName(Path::PathFromUtf8(Reference).extension()) != u8".uplugin")
		{
			throw std::runtime_error(Field + ".Path must identify a .uplugin descriptor.");
		}
		Selected[ResolvePlugin(Root, Reference, Available)].merge(ReadExclusions(Entry, Field + ".ExcludeModules"));
	}
	return Selected;
}
} // namespace

std::set<std::string> ExportModules::RulesFiles() const
{
	std::set<std::string> Rules;
	for (const auto& [Name, Directory] : Modules)
	{
		Rules.insert(Path::PathToUtf8(Directory / (Name + ".Build.cs")));
	}
	return Rules;
}

ExportModules ExportModules::Discover(const fs::path& Project)
{
	const auto ConfigFile = fs::path(Project).replace_extension(".modexport");
	const auto Root = Project.parent_path();
	try
	{
		const auto Config =
		    fs::exists(ConfigFile) ? Json::parse(BuildTool::File::ReadFile(ConfigFile)) : Json::object();
		if (!Config.is_object())
		{
			throw std::runtime_error("Expected an object with Plugins and/or ExcludeModules.");
		}
		for (const auto& Key : Config.items())
		{
			if (Key.key() != "Plugins" && Key.key() != "ExcludeModules")
			{
				throw std::runtime_error("Unknown field '" + Key.key() + "'.");
			}
		}
		ExportModules Result;
		const auto GlobalExclusions = ReadExclusions(Config, "ExcludeModules");
		ModuleNames Names;
		const auto AddModules = [&](const fs::path& Tree, const ModuleNames& LocalExclusions)
		{
			for (const auto& Rules : FindRoots(Tree, u8".build.cs"))
			{
				const auto Name = Rules.stem().stem();
				const auto Folded = PathName::LowercaseName(Name);
				if (GlobalExclusions.contains(Folded) || LocalExclusions.contains(Folded))
				{
					continue;
				}
				if (!Names.insert(Folded).second)
				{
					throw std::runtime_error("Duplicate module name: " + Path::PathToUtf8(Rules));
				}
				Result.Modules.emplace(Path::PathToUtf8(Name), Rules.parent_path());
			}
		};
		AddModules(Root / "Source", {});
		for (const auto& [Descriptor, Exclusions] : ReadPlugins(Root, Config.value("Plugins", Json::array())))
		{
			Result.PluginDescriptors.push_back(Descriptor);
			AddModules(Descriptor.parent_path() / "Source", Exclusions);
		}
		return Result;
	}
	catch (const std::exception& Error)
	{
		throw std::runtime_error(Path::PathToUtf8(ConfigFile) + ": " + Error.what());
	}
}
} // namespace Mod::Tools
