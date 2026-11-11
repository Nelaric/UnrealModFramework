// Copyright (c) 2026 Nelaric Contributors
#include "OptionsCheck.h"

#include "ModHeaderTool/src/Metadata/MetadataOptionsReader.h"
#include "ModHeaderTool/src/Metadata/MetadataOptionsRefreshRequired.h"
#include "ModHeaderTool/src/Project/Project.h"
#include "ModHeaderTool/src/Project/ProjectReader.h"

#include <filesystem>
#include <iostream>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

int OptionsCheck::CheckOptions(const fs::path& ProjectFile, const fs::path& Snapshot)
{
	const Project Project = ProjectReader::LoadProject(ProjectFile);
	try
	{
		MetadataOptionsReader::LoadMetadataOptions(Project, Snapshot);
	}
	catch (const MetadataOptionsRefreshRequired& Error)
	{
		std::clog << "ModHeaderTool: " << Error.Reason << '\n';
		return 2;
	}
	return 0;
}
} // namespace Mod::HeaderTool
