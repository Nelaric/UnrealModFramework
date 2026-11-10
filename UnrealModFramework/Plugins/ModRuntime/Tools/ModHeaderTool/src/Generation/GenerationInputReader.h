// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationInputReader.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class GenerationInputReader
{
public:
	static GenerationInput LoadGenerationInput(const fs::path& File);
};
} // namespace Mod::HeaderTool
