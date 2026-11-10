// Copyright (c) 2026 Nelaric Contributors
/** @file AnalysisOptions.h */
#pragma once

#include <string>
#include <vector>

namespace Mod::SourceTool
{
struct AnalysisOptions
{
	std::string Catalog;
	std::string MemberId;
	std::string CompilationDatabase;
	std::string Target;
	std::string ClangLibrary;
	std::string ResourceDirectory;
	std::string Output;
	std::vector<std::string> AnalysisArguments;
	bool bUpdateCatalog = false;
};
} // namespace Mod::SourceTool
