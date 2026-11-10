// Copyright (c) 2026 Nelaric Contributors
/** @file DeclarationAnalyzer.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class DeclarationAnalyzer
{
public:
	static GenerationModel Analyze(GenerationInput Input, const fs::path& Options, const fs::path& ClangLibrary,
	                               const fs::path& AnalysisDatabase);
};
} // namespace Mod::HeaderTool
