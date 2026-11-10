// Copyright (c) 2026 Nelaric Contributors
/** @file ArtifactEmitter.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ArtifactEmitter
{
public:
	static void Emit(GenerationModel Model, const fs::path& OutputDirectory);
};
} // namespace Mod::HeaderTool
