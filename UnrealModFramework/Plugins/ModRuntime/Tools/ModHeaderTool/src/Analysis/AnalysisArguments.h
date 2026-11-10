// Copyright (c) 2026 Nelaric Contributors
/** @file AnalysisArguments.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class AnalysisArguments
{
public:
	static std::vector<std::string> SplitArguments(std::string_view Text);
	static std::vector<std::string> AnalysisCommand(const std::vector<std::string>& Command, const fs::path& Directory,
	                                                const fs::path& Unit, GenerationModel& Model);

private:
	static void ExpandResponses(const std::vector<std::string>& Arguments, const fs::path& Directory,
	                            std::vector<std::string>& Expanded, std::set<fs::path>& Stack, GenerationModel& Model);
};
} // namespace Mod::HeaderTool
