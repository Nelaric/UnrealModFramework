// Copyright (c) 2026 Nelaric Contributors
#include "ProjectScanner.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Metadata/MetadataOptionsReader.h"
#include "ModHeaderTool/src/Metadata/MetadataParser.h"
#include "ModHeaderTool/src/Metadata/MetadataValidator.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Project/ProjectReader.h"
#include "ModHeaderTool/src/Project/ProjectSources.h"
#include "ModHeaderTool/src/Scan/MacroScanner.h"

#include "ModExportMetadataSchema.h"

#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

ProjectScanner::ProjectScanner(const fs::path& Input, const fs::path& OptionsFile)
{
	Result.Project = ProjectReader::LoadProject(Input);
	Result.SourceFiles = ProjectSources::CollectSourceFiles(Result.Project);
	const bool bHasSnapshot = fs::is_regular_file(OptionsFile);
	CatalogPath =
	    bHasSnapshot ? fs::absolute(OptionsFile) : Result.Project.Root / Mod::ExportMetadata::Schema::SnapshotPath;
	if (bHasSnapshot)
	{
		LoadOptions();
	}
}

ScanResult ProjectScanner::Run() &&
{
	for (const fs::path& SourceFile : Result.SourceFiles)
	{
		ScanFile(SourceFile);
	}
	return std::move(Result);
}

void ProjectScanner::LoadOptions()
{
	bAttemptedOptions = true;
	Options = MetadataOptionsReader::LoadMetadataOptions(Result.Project, CatalogPath);
	Result.MetadataOptions = Options->File;
}

void ProjectScanner::ScanFile(const fs::path& SourceFile)
{
	try
	{
		const std::string Source = TextFile::ReadTextFile(SourceFile);
		for (ExportMacro& Macro : MacroScanner::ScanSource(SourceFile, Source))
		{
			try
			{
				ValidateExport(Macro, Source);
			}
			catch (const std::exception& Error)
			{
				Result.Diagnostics.push_back(Error.what());
			}
			Result.Exports.push_back(std::move(Macro));
		}
	}
	catch (const std::exception& Error)
	{
		Result.Diagnostics.push_back(Error.what());
	}
}

void ProjectScanner::ValidateExport(ExportMacro& Macro, const std::string& Source)
{
	Macro.Fields = MetadataParser::ParseMetadata(Macro, Source);
	if (!bAttemptedOptions)
	{
		try
		{
			LoadOptions();
		}
		catch (const std::exception& Error)
		{
			Result.Diagnostics.push_back(
			    DiagnosticLocation::FormatDiagnosticLocation(Macro.File, Macro.Line, Macro.Column) +
			    ": error: " + Macro.Name + ": " + Error.what());
		}
	}
	for (std::string& Error : MetadataValidator::ValidateMetadata(Macro, Options ? &*Options : nullptr, Source))
	{
		Result.Diagnostics.push_back(std::move(Error));
	}
}
} // namespace Mod::HeaderTool
