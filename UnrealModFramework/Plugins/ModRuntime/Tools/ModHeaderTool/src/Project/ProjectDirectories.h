// Copyright (c) 2026 Nelaric Contributors
/** @file ProjectDirectories.h */
#pragma once

#include "ModHeaderTool/src/Project/Project.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ProjectDirectories
{
public:
	static void AddDirectory(Project& Project, const fs::path& Path, bool bRequired);
	static void LoadDirectories(Project& Project);
};
} // namespace Mod::HeaderTool
