// Copyright (c) 2026 Nelaric Contributors
#include "MetadataOptionsReader.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Metadata/MetadataOptionsRefreshRequired.h"
#include "ModHeaderTool/src/Metadata/MetadataSnapshot.h"
#include "ModHeaderTool/src/Metadata/RedirectReader.h"

#include "ModExportMetadataSchema.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace Schema = Mod::ExportMetadata::Schema;

MetadataOptions MetadataOptionsReader::LoadMetadataOptions(const Project& Project, const fs::path& File)
{
	if (!fs::is_regular_file(File))
	{
		throw MetadataOptionsRefreshRequired(
		    "Metadata option catalog not found: " + DiagnosticLocation::QuoteDiagnosticPath(File),
		    ". Run UnrealEditor-Cmd <project> -run=ModExportOptions, or pass --options <catalog.json>.");
	}
	const Json Document = Json::parse(TextFile::ReadTextFile(File));
	const int Version = Document.is_object() ? Document.value("SchemaVersion", 0) : -1;
	if (Version <= 0 || Version > Schema::SnapshotVersion)
	{
		throw std::runtime_error("Unsupported metadata option catalog schema: " +
		                         DiagnosticLocation::QuoteDiagnosticPath(File));
	}
	if (Version < Schema::SnapshotVersion)
	{
		throw MetadataOptionsRefreshRequired("Outdated metadata option catalog schema: " +
		                                         DiagnosticLocation::QuoteDiagnosticPath(File),
		                                     MetadataSnapshot::RefreshHint);
	}
	if (!Document.contains("Project") || !Document["Project"].is_string())
	{
		throw std::runtime_error("Metadata option catalog must identify its project.");
	}
	const fs::path CatalogProject = Path::PathFromUtf8(Document["Project"].get<std::string>());
	if (!fs::exists(CatalogProject) || !fs::equivalent(CatalogProject, Project.File))
	{
		throw std::runtime_error("Metadata option catalog belongs to a different project.");
	}
	MetadataSnapshot::CheckInputs(Document);
	if (!Document.contains("Options") || !Document["Options"].is_object())
	{
		throw std::runtime_error("Metadata option catalog requires an 'Options' object.");
	}
	MetadataOptions Result;
	Result.File = fs::canonical(File);
	Result.ConfigRoots = MetadataSnapshot::PathArray(Document, "ConfigRoots");
	if (!Document.contains("FunctionRedirects") || !Document["FunctionRedirects"].is_array())
	{
		throw std::runtime_error("Metadata options require a 'FunctionRedirects' array.");
	}
	for (const auto& Redirect : Document["FunctionRedirects"])
	{
		Result.FunctionRedirects.push_back(
		    {RedirectReader::RedirectName(Redirect.at("OldName")), RedirectReader::RedirectName(Redirect.at("NewName")),
		     RedirectReader::RedirectLocation(Redirect.at("Location").get_ref<const std::string&>()),
		     Redirect.at("Entry").get<std::string>(), Redirect.at("Pattern").get<bool>(),
		     Redirect.at("HasNewName").get<bool>()});
	}
	for (const auto& Entry : Document["Inputs"].items())
	{
		Result.Inputs.push_back(Path::PathFromUtf8(Entry.key()));
	}
	for (const auto& Entry : Document["Options"].items())
	{
		if (!Entry.value().is_array())
		{
			throw std::runtime_error("Metadata choices must be string arrays: " + Entry.key());
		}
		std::vector<std::string>& Values = Result.Choices[Entry.key()];
		for (const Json& Value : Entry.value())
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
	for (const char* Key : {"APIScopes", "APIThreads", "APIPermissions", "APIFailureRules", "EventSourceScopes",
	                        "EventPublishRules", "HookStages", "HookOperations"})
	{
		if (!Result.Choices.contains(Key))
		{
			throw std::runtime_error(std::string("Missing metadata choice group '") + Key + "'.");
		}
	}
	return Result;
}
} // namespace Mod::HeaderTool
