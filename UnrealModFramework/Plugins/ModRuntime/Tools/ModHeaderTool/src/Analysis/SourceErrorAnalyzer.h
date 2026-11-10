// Copyright (c) 2026 Nelaric Contributors
/** @file SourceErrorAnalyzer.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class SourceErrorAnalyzer
{
public:
	static void AnalyzeSourceErrors(GenerationModel& Model, const fs::path& AnalysisDatabase,
	                                const fs::path& ClangLibrary, const fs::path& SourceTool);
};
} // namespace Mod::HeaderTool
