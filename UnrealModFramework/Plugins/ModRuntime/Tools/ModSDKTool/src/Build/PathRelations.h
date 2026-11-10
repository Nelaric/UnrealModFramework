// Copyright (c) 2026 Nelaric Contributors
/** @file PathRelations.h */
#pragma once

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class PathRelations
{
public:
	/// Compares normalized paths, ignoring case on Windows.
	static bool SamePath(const fs::path& Left, const fs::path& Right);
	/// Returns whether a normalized path is within the given directory.
	static bool IsInside(const fs::path& Path, const fs::path& Root);
};
} // namespace Mod::SdkTool
