// Copyright (c) 2026 Nelaric Contributors
#include "ScanReport.h"

#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace
{
std::string DisplaySourcePath(const fs::path& File, const fs::path& Root)
{
	const fs::path Relative = File.lexically_relative(Root);
	return Path::PathToUtf8(!Relative.empty() && *Relative.begin() != ".." ? Relative : File);
}
} // namespace

void ScanReport::WriteScanReport(const ScanResult& Result)
{
	if (!Result.Diagnostics.empty())
	{
		throw std::runtime_error("Cannot write a successful report for a scan containing errors.");
	}
	Json Directories = Json::array();
	Json Watches = Json::array({{{"root", Path::PathToUtf8(Result.Project.File)},
	                             {"kind", "ExportModules"},
	                             {"files", Result.Project.Selection.RulesFiles()}}});
	for (const fs::path& Directory : Result.Project.Directories)
	{
		Directories.push_back(Path::PathToUtf8(Directory));
		Json Files = Json::array();
		for (const auto& File : Result.SourceFiles)
		{
			const auto Relative = File.lexically_relative(Directory);
			if (!Relative.empty() && *Relative.begin() != "..")
			{
				Files.push_back(Path::PathToUtf8(File));
			}
		}
		Watches.push_back({{"root", Path::PathToUtf8(Directory)}, {"kind", "Source"}, {"files", Files}});
	}
	Json Exports = Json::array();
	for (const ExportMacro& Macro : Result.Exports)
	{
		Json Metadata = Json::object();
		for (const MetadataField& Field : Macro.Fields)
		{
			if (Field.Value.Kind == MetadataValue::EKind::List)
			{
				Json Items = Json::array();
				for (const MetadataValue& Item : Field.Value.Items)
				{
					Items.push_back(Item.Text);
				}
				Metadata[Field.Name] = std::move(Items);
			}
			else if (Field.Value.Kind == MetadataValue::EKind::Flag)
			{
				Metadata[Field.Name] = true;
			}
			else
			{
				Metadata[Field.Name] = Field.Value.Text;
			}
		}
		Exports.push_back({{"Macro", Macro.Name},
		                   {"File", DisplaySourcePath(Macro.File, Result.Project.Root)},
		                   {"Line", Macro.Line},
		                   {"Column", Macro.Column},
		                   {"Offset", Macro.Offset},
		                   {"Length", Macro.Length},
		                   {"Arguments", Macro.Arguments},
		                   {"Metadata", std::move(Metadata)}});
	}
	const Json Report = {
	    {"SchemaVersion", 2},
	    {"Project", Path::PathToUtf8(Result.Project.File)},
	    {"ProjectRoot", Path::PathToUtf8(Result.Project.Root)},
	    {"Config", Result.Project.bHasConfig ? Json(Path::PathToUtf8(Result.Project.ConfigFile)) : Json(nullptr)},
	    {"Directories", Directories},
	    {"WatchedDirectories", Watches},
	    {"ScannedFileCount", Result.SourceFiles.size()},
	    {"PreprocessorEvaluated", false},
	    {"ParametersValidated", true},
	    {"MetadataOptions",
	     Result.MetadataOptions.empty() ? Json(nullptr) : Json(Path::PathToUtf8(Result.MetadataOptions))},
	    {"Exports", Exports}};
	std::cout << Report.dump(2) << '\n' << std::flush;
	if (!std::cout)
	{
		throw std::runtime_error("Cannot write scan result to stdout.");
	}
}
} // namespace Mod::HeaderTool
