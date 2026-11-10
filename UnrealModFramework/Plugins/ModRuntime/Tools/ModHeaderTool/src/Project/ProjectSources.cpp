// Copyright (c) 2026 Nelaric Contributors
#include "ProjectSources.h"

#include "Common/Files/WatchedFiles.h"

#include <filesystem>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

std::vector<fs::path> ProjectSources::CollectSourceFiles(const Project& Project)
{
	return Tools::WatchedFiles::CollectSourceFiles(Project.Directories);
}
} // namespace Mod::HeaderTool
