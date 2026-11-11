// Copyright (c) 2026 Nelaric Contributors
/** @file AnalysisOptions.h */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
struct AnalysisOptions
{
	static AnalysisOptions Read(const std::filesystem::path& File);

	std::string Catalog;
	std::string MemberId;
	std::string CompilationDatabase;
	std::string Target;
	std::string ClangLibrary;
	std::string ResourceDirectory;
	std::vector<std::string> AnalysisArguments;
};
} // namespace Mod::SourceTool
