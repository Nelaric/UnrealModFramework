// Copyright (c) 2026 Nelaric Contributors
/** @file ProjectReader.h */
#pragma once

#include "ModHeaderTool/src/Project/Project.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ProjectReader
{
public:
	/// Resolves a .uproject file or a directory containing exactly one.
	static Project LoadProject(const fs::path& Input);
};
} // namespace Mod::HeaderTool
