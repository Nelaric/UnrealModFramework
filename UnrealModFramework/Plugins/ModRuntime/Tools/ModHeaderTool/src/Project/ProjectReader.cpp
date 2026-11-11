// Copyright (c) 2026 Nelaric Contributors
#include "ProjectReader.h"

#include "Common/Files/PathName.h"
#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Project/ProjectDirectories.h"

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

Project ProjectReader::LoadProject(const fs::path& Input)
{
	fs::path ProjectFile = fs::absolute(Input).lexically_normal();
	if (fs::is_directory(ProjectFile))
	{
		std::vector<fs::path> Projects;
		for (const fs::directory_entry& Entry : fs::directory_iterator(ProjectFile))
		{
			if (Entry.is_regular_file() && Tools::PathName::LowercaseName(Entry.path().extension()) == u8".uproject")
			{
				Projects.push_back(Entry.path());
			}
		}
		if (Projects.size() != 1)
		{
			throw std::runtime_error("Expected exactly one .uproject in " +
			                         DiagnosticLocation::QuoteDiagnosticPath(ProjectFile) +
			                         ". Specify the .uproject file directly.");
		}
		ProjectFile = Projects.front();
	}
	if (!fs::is_regular_file(ProjectFile) || Tools::PathName::LowercaseName(ProjectFile.extension()) != u8".uproject")
	{
		throw std::runtime_error("Project must be an existing .uproject file: " +
		                         DiagnosticLocation::QuoteDiagnosticPath(ProjectFile));
	}
	Project Project;
	Project.File = fs::canonical(ProjectFile);
	Project.Root = Project.File.parent_path();
	Project.ConfigFile = Project.File;
	Project.ConfigFile.replace_extension(".modexpot");
	ProjectDirectories::AddDirectory(Project, Project.Root / "Plugins", false);
	ProjectDirectories::AddDirectory(Project, Project.Root / "Source", false);
	Project.bHasConfig = fs::exists(Project.ConfigFile);
	if (Project.bHasConfig)
	{
		ProjectDirectories::LoadDirectories(Project);
	}
	return Project;
}
} // namespace Mod::HeaderTool
