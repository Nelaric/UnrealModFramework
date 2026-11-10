// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationDestination.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class GenerationDestination
{
public:
	static fs::path ResolveOutputRoot(const GenerationModel& Model, const fs::path& OutputDirectory);
};
} // namespace Mod::HeaderTool
