// Copyright (c) 2026 Nelaric Contributors
/** @file CompiledObjects.h */
#pragma once

#include <filesystem>
#include <set>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

struct CompiledObjects
{
	std::vector<fs::path> Paths;
	std::set<fs::path> Dependencies;
};
} // namespace Mod::BuildTool
