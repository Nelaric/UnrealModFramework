// Copyright (c) 2026 Nelaric Contributors
/** @file Source.h */
#pragma once

#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct Source
{
	fs::path Path;
	std::string Text;
	std::vector<ExportMacro> Macros;
	bool bCovered = false;
	std::set<std::size_t> Active;
};
} // namespace Mod::HeaderTool
