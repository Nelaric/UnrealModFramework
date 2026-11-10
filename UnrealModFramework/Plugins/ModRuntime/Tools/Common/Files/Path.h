// Copyright (c) 2026 Nelaric Contributors
/** @file Path.h */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class Path
{
public:
	/// Converts a native path to UTF-8.
	static std::string PathToUtf8(const fs::path& Path);
	/// Converts UTF-8 to a native path.
	static fs::path PathFromUtf8(std::string_view Text);
	/// Resolves a path relative to an explicit input directory.
	static fs::path ResolvePath(const fs::path& Base, std::string_view Text);
	/// Requires a generated file to remain inside its declared root.
	static fs::path ResolveGeneratedPath(const fs::path& Root, std::string_view Text);
};
} // namespace Mod::BuildTool
