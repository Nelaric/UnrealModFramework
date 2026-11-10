// Copyright (c) 2026 Nelaric Contributors
/** @file BinaryInspector.h */
#pragma once

#include <filesystem>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class BinaryInspector
{
public:
	/// Checks a linked PE, ELF, or thin Mach-O image's kind and architecture.
	static void ValidateHostBinary(const fs::path& Path, std::string_view Platform, std::string_view Architecture,
	                               std::string_view Kind, std::string_view LinkWitness = {});
};
} // namespace Mod::BuildTool
