// Copyright (c) 2026 Nelaric Contributors
/** @file Project.h */
#pragma once

#include "ModHeaderTool/FileSystem.h"

#include <vector>

namespace Mod::HeaderTool
{
/// A project and its resolved, existing source scan directories.
struct FProject
{
	/// Canonical project descriptor path.
	fs::path File;
	/// Directory containing the project descriptor.
	fs::path Root;
	/// Optional configuration path beside the project descriptor.
	fs::path ConfigFile;
	/// Whether the optional configuration exists.
	bool bHasConfig = false;
	/// Existing, canonical scan roots.
	std::vector<fs::path> Directories;
};

/// Resolves a .uproject file or a directory containing exactly one.
FProject LoadProject(const fs::path& Input);

/// Collects source files once each, following links without revisiting dirs.
std::vector<fs::path> CollectSourceFiles(const FProject& Project);
} // namespace Mod::HeaderTool
