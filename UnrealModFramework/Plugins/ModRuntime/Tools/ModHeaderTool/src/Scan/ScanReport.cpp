// Copyright (c) 2026 Nelaric Contributors
#include "ScanReport.h"

#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"
#include "ModHeaderTool/src/Scan/ScanDocument.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void ScanReport::WriteScanReport(const ScanResult& Result, const fs::path& Output)
{
	if (!Result.Diagnostics.empty())
	{
		throw std::runtime_error("Cannot write a successful report for a scan containing errors.");
	}
	using Json = nlohmann::json;
	Json Directories = Json::array();
	for (const fs::path& Directory : Result.Project.Directories)
	{
		Directories.push_back(Path::PathToUtf8(Directory));
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
			else
			{
				Metadata[Field.Name] = Field.Value.Text;
			}
		}
		Exports.push_back({{"Macro", Macro.Name},
		                   {"File", ScanDocument::DisplaySourcePath(Macro.File, Result.Project.Root)},
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
	    {"ScannedFileCount", Result.SourceFiles.size()},
	    {"PreprocessorEvaluated", false},
	    {"ParametersValidated", true},
	    {"MetadataOptions",
	     Result.MetadataOptions.empty() ? Json(nullptr) : Json(Path::PathToUtf8(Result.MetadataOptions))},
	    {"Exports", Exports}};
	ScanDocument::WriteDocument(Report.dump(2), Output, Result);
}
} // namespace Mod::HeaderTool
