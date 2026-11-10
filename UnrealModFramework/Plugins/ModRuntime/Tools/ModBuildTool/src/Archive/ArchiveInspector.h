// Copyright (c) 2026 Nelaric Contributors
/** @file ArchiveInspector.h */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class ArchiveInspector
{
public:
	/// Requires native objects and every requested symbol in the archive index.
	static void ValidateHostArchive(const fs::path& Path, std::string_view Platform, std::string_view Architecture,
	                                const std::vector<std::string>& RequiredSymbols);
};
} // namespace Mod::BuildTool
