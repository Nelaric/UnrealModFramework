// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/MetadataOptions.h"

#include "ModExportMetadataSchema.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
namespace
{
namespace Schema = Mod::ExportMetadata::Schema;
using FJson = nlohmann::json;

std::string InputPath(const fs::path& Path)
{
	if (!Path.is_absolute())
	{
		throw std::runtime_error("Metadata option input paths must be absolute.");
	}
	return PathToUtf8(fs::weakly_canonical(Path));
}

std::string FileFingerprint(const fs::path& Path)
{
	if (!fs::exists(Path))
	{
		return {};
	}
	std::ostringstream Result;
	Result << std::hex << std::setfill('0') << std::setw(16) << Schema::Fingerprint(ReadTextFile(Path));
	return Result.str();
}

std::vector<fs::path> PathArray(const FJson& Document, const char* Field)
{
	if (!Document.contains(Field) || !Document[Field].is_array())
	{
		throw std::runtime_error(std::string("Metadata options require a '") + Field + "' path array.");
	}
	std::vector<fs::path> Result;
	for (const FJson& Value : Document[Field])
	{
		if (!Value.is_string() || Value.get_ref<const std::string&>().empty())
		{
			throw std::runtime_error(std::string("Invalid path in metadata '") + Field + "'.");
		}
		const fs::path Path = PathFromUtf8(Value.get_ref<const std::string&>());
		(void)InputPath(Path);
		Result.push_back(Path);
	}
	return Result;
}

void CheckInputs(const FJson& Document)
{
	if (!Document.contains("Inputs") || !Document["Inputs"].is_object())
	{
		throw std::runtime_error("Metadata options require configuration input fingerprints.");
	}
	std::map<std::string, std::string> Expected;
	for (const auto& Input : Document["Inputs"].items())
	{
		if (!Input.value().is_string())
		{
			throw std::runtime_error("Invalid metadata input fingerprint.");
		}
		Expected.emplace(InputPath(PathFromUtf8(Input.key())), Input.value().get<std::string>());
	}
	std::map<std::string, std::string> Current;
	for (const fs::path& Root : PathArray(Document, "ConfigRoots"))
	{
		if (!fs::exists(Root))
		{
			continue;
		}
		for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Root))
		{
			std::string Extension = PathToUtf8(Entry.path().extension());
			std::transform(Extension.begin(), Extension.end(), Extension.begin(),
			               [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
			if (Entry.is_regular_file() && Extension == ".ini")
			{
				Current.emplace(InputPath(Entry.path()), FileFingerprint(Entry.path()));
			}
		}
	}
	for (const fs::path& Input : PathArray(Document, "InputFiles"))
	{
		Current.emplace(InputPath(Input), FileFingerprint(Input));
	}
	if (Current != Expected)
	{
		std::string Changed;
		for (const auto& [Path, Hash] : Current)
		{
			const auto Existing = Expected.find(Path);
			if (Existing == Expected.end() || Existing->second != Hash)
			{
				Changed = Path;
				break;
			}
		}
		if (Changed.empty())
		{
			for (const auto& [Path, Hash] : Expected)
			{
				if (!Current.contains(Path))
				{
					Changed = Path;
					break;
				}
			}
		}
		throw std::runtime_error("Metadata options are stale because an input changed: " + Changed +
		                         ". Run the UE ModExportOptions commandlet again.");
	}
}
} // namespace

FMetadataOptions LoadMetadataOptions(const FProject& Project, const fs::path& File)
{
	if (!fs::is_regular_file(File))
	{
		throw std::runtime_error(
		    "Metadata option catalog not found: " + PathToUtf8(File) +
		    ". Run UnrealEditor-Cmd <project> -run=ModExportOptions, or pass --options <catalog.json>.");
	}
	const FJson Document = FJson::parse(ReadTextFile(File));
	if (!Document.is_object() || Document.value("SchemaVersion", 0) != Schema::SnapshotVersion)
	{
		throw std::runtime_error("Unsupported metadata option catalog schema.");
	}
	if (!Document.contains("Project") || !Document["Project"].is_string())
	{
		throw std::runtime_error("Metadata option catalog must identify its project.");
	}
	const fs::path CatalogProject = PathFromUtf8(Document["Project"].get<std::string>());
	if (!fs::exists(CatalogProject) || !fs::equivalent(CatalogProject, Project.File))
	{
		throw std::runtime_error("Metadata option catalog belongs to a different project.");
	}
	CheckInputs(Document);
	if (!Document.contains("Options") || !Document["Options"].is_object())
	{
		throw std::runtime_error("Metadata option catalog requires an 'Options' object.");
	}
	FMetadataOptions Result;
	Result.File = fs::canonical(File);
	Result.ConfigRoots = PathArray(Document, "ConfigRoots");
	for (const auto& Entry : Document["Inputs"].items())
	{
		Result.Inputs.push_back(PathFromUtf8(Entry.key()));
	}
	for (const auto& Entry : Document["Options"].items())
	{
		if (!Entry.value().is_array())
		{
			throw std::runtime_error("Metadata choices must be string arrays: " + Entry.key());
		}
		std::vector<std::string>& Values = Result.Choices[Entry.key()];
		for (const FJson& Value : Entry.value())
		{
			if (!Value.is_string() || Value.get_ref<const std::string&>().empty())
			{
				throw std::runtime_error("Metadata choices must be nonempty strings: " + Entry.key());
			}
			const std::string Text = Value.get<std::string>();
			if (std::find(Values.begin(), Values.end(), Text) == Values.end())
			{
				Values.push_back(Text);
			}
		}
	}
	for (const char* Key : {"ExportIds", "ApiScopes", "ApiThreads", "ApiPermissions", "ApiFailureRules",
	                        "EventSourceScopes", "EventPublishRules", "HookStages"})
	{
		if (!Result.Choices.contains(Key))
		{
			throw std::runtime_error(std::string("Missing metadata choice group '") + Key + "'.");
		}
	}
	return Result;
}
} // namespace Mod::HeaderTool
