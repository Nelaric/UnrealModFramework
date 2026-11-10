// Copyright (c) 2026 Nelaric Contributors
/** @file Path.h */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class Path
{
public:
	/// Converts a filesystem path to UTF-8 with forward slashes.
	static std::string PathToUtf8(const fs::path& Path);
	/// Converts a UTF-8 command-line or JSON path to a native path.
	static fs::path PathFromUtf8(std::string_view Text);
};
} // namespace Mod::HeaderTool
