// Copyright (c) 2026 Nelaric Contributors
#include "OptionsCheck.h"

#include "ModHeaderTool/src/Metadata/MetadataOptionsReader.h"
#include "ModHeaderTool/src/Metadata/MetadataOptionsRefreshRequired.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Project/ProjectReader.h"

#include "ModExportMetadataSchema.h"

#include <filesystem>
#include <iostream>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

int OptionsCheck::CheckOptions(const CommandLineOptions& Options)
{
	const Project Project = ProjectReader::LoadProject(Options.Project);
	const fs::path Catalog =
	    Options.Options.empty() ? Project.Root / Mod::ExportMetadata::Schema::SnapshotPath : Options.Options;
	try
	{
		MetadataOptionsReader::LoadMetadataOptions(Project, Catalog);
	}
	catch (const MetadataOptionsRefreshRequired& Error)
	{
		std::clog << "ModHeaderTool: " << Error.Reason << '\n';
		return 2;
	}
	return 0;
}
} // namespace Mod::HeaderTool
