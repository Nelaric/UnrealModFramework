// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/ScanReport.h"

#include "ModExportMetadataSchema.h"
#include "ModHeaderTool/FileSystem.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
namespace
{
std::string DisplaySourcePath(const fs::path& Path, const fs::path& Root)
{
	const fs::path Relative = Path.lexically_relative(Root);
	if (!Relative.empty() && *Relative.begin() != "..")
	{
		return PathToUtf8(Relative);
	}
	return PathToUtf8(Path);
}

void WriteDocument(const std::string& Document, const fs::path& Output, const FScanResult& Result)
{
	if (Output.empty())
	{
		std::cout << Document << '\n';
		if (!std::cout)
		{
			throw std::runtime_error("Cannot write scan result to stdout.");
		}
		return;
	}
	const fs::path Destination = fs::absolute(Output).lexically_normal();
	if (fs::exists(Destination))
	{
		bool bIsInput = fs::equivalent(Destination, Result.Project.File) ||
		                (Result.Project.bHasConfig && fs::equivalent(Destination, Result.Project.ConfigFile));
		const fs::path Catalog = Result.MetadataOptions.empty()
		                             ? Result.Project.Root / Mod::ExportMetadata::Schema::SnapshotPath
		                             : Result.MetadataOptions;
		bIsInput = bIsInput || (fs::exists(Catalog) && fs::equivalent(Destination, Catalog));
		for (const fs::path& SourceFile : Result.SourceFiles)
		{
			bIsInput = bIsInput || fs::equivalent(Destination, SourceFile);
		}
		if (bIsInput)
		{
			throw std::runtime_error("Output would overwrite a scan input: " + PathToUtf8(Destination));
		}
	}
	fs::create_directories(Destination.parent_path());
	std::ofstream File(Destination, std::ios::binary | std::ios::trunc);
	File << Document << '\n';
	File.close();
	if (!File)
	{
		throw std::runtime_error("Cannot write scan result: " + PathToUtf8(Destination));
	}
}
} // namespace

void WriteScanReport(const FScanResult& Result, const fs::path& Output)
{
	if (!Result.Diagnostics.empty())
	{
		throw std::runtime_error("Cannot write a successful report for a scan containing errors.");
	}
	using FJson = nlohmann::json;
	FJson Directories = FJson::array();
	for (const fs::path& Directory : Result.Project.Directories)
	{
		Directories.push_back(PathToUtf8(Directory));
	}
	FJson Exports = FJson::array();
	for (const FExportMacro& Macro : Result.Exports)
	{
		FJson Metadata = FJson::object();
		for (const FMetadataField& Field : Macro.Fields)
		{
			if (Field.Value.Kind == EMetadataValueKind::List)
			{
				FJson Items = FJson::array();
				for (const FMetadataValue& Item : Field.Value.Items)
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
		                   {"File", DisplaySourcePath(Macro.File, Result.Project.Root)},
		                   {"Line", Macro.Line},
		                   {"Column", Macro.Column},
		                   {"Offset", Macro.Offset},
		                   {"Length", Macro.Length},
		                   {"Arguments", Macro.Arguments},
		                   {"Metadata", std::move(Metadata)}});
	}
	const FJson Report = {
	    {"SchemaVersion", 2},
	    {"Project", PathToUtf8(Result.Project.File)},
	    {"ProjectRoot", PathToUtf8(Result.Project.Root)},
	    {"Config", Result.Project.bHasConfig ? FJson(PathToUtf8(Result.Project.ConfigFile)) : FJson(nullptr)},
	    {"Directories", Directories},
	    {"ScannedFileCount", Result.SourceFiles.size()},
	    {"PreprocessorEvaluated", false},
	    {"ParametersValidated", true},
	    {"MetadataOptions",
	     Result.MetadataOptions.empty() ? FJson(nullptr) : FJson(PathToUtf8(Result.MetadataOptions))},
	    {"Exports", Exports}};
	WriteDocument(Report.dump(2), Output, Result);
}
} // namespace Mod::HeaderTool
