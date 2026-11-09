// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/Scanner.h"

#include "ModExportMetadataSchema.h"
#include "ModHeaderTool/MetadataOptions.h"
#include "ModHeaderTool/MetadataParser.h"
#include "ModHeaderTool/MetadataValidator.h"

#include <exception>
#include <optional>
#include <utility>

namespace Mod::HeaderTool
{
FScanResult ScanProject(const fs::path& Input, const fs::path& OptionsFile)
{
	FScanResult Result;
	Result.Project = LoadProject(Input);
	Result.SourceFiles = CollectSourceFiles(Result.Project);
	const fs::path CatalogPath = OptionsFile.empty() ? Result.Project.Root / Mod::ExportMetadata::Schema::SnapshotPath
	                                                 : fs::absolute(OptionsFile);
	std::optional<FMetadataOptions> Options;
	bool bAttemptedOptions = false;
	if (!OptionsFile.empty())
	{
		Options = LoadMetadataOptions(Result.Project, CatalogPath);
		Result.MetadataOptions = Options->File;
		bAttemptedOptions = true;
	}
	for (const fs::path& SourceFile : Result.SourceFiles)
	{
		try
		{
			const std::string Source = ReadTextFile(SourceFile);
			for (FExportMacro& Macro : ScanSource(SourceFile, Source))
			{
				try
				{
					Macro.Fields = ParseMetadata(Macro, Source);
					if (!bAttemptedOptions)
					{
						bAttemptedOptions = true;
						try
						{
							Options = LoadMetadataOptions(Result.Project, CatalogPath);
							Result.MetadataOptions = Options->File;
						}
						catch (const std::exception& Error)
						{
							Result.Diagnostics.push_back(PathToUtf8(Macro.File) + "(" + std::to_string(Macro.Line) +
							                             "," + std::to_string(Macro.Column) +
							                             "): error: " + Macro.Name + ": " + Error.what());
						}
					}
					for (std::string& Error : ValidateMetadata(Macro, Options ? &*Options : nullptr, Source))
					{
						Result.Diagnostics.push_back(std::move(Error));
					}
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
	return Result;
}
} // namespace Mod::HeaderTool
