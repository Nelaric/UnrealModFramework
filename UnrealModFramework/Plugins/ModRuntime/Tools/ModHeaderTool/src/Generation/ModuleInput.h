// Copyright (c) 2026 Nelaric Contributors
/** @file ModuleInput.h */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct ModuleInput
{
	std::string Name;
	fs::path Root;
	std::vector<fs::path> TranslationUnits;

	/// Header spelling relative to the owner's UE include directories.
	std::string HeaderInclude(const fs::path& File) const;
};
} // namespace Mod::HeaderTool
