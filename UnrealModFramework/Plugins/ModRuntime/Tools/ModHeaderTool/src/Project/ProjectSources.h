// Copyright (c) 2026 Nelaric Contributors
/** @file ProjectSources.h */
#pragma once

#include "ModHeaderTool/src/Project/Project.h"

#include <filesystem>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ProjectSources
{
public:
	/// Collects source files once each, following links without revisiting dirs.
	static std::vector<fs::path> CollectSourceFiles(const Project& Project);
};
} // namespace Mod::HeaderTool
