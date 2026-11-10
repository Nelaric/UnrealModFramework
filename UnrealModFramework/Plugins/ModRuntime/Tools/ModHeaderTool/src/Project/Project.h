// Copyright (c) 2026 Nelaric Contributors
/** @file Project.h */
#pragma once

#include <filesystem>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

/// A project and its resolved, existing source scan directories.
struct Project
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
} // namespace Mod::HeaderTool
