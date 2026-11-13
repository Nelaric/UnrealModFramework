// Copyright (c) 2026 Nelaric Contributors
/** @file Traversal.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"

#include <clang-c/Index.h>
#include <exception>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
struct Traversal
{
	ClangLibrary& Clang;
	CXTranslationUnit Tu;
	const std::map<std::string, std::filesystem::path>& SourcePaths;
	std::map<std::string, std::vector<CXCursor>> Methods;
	std::map<std::string, std::vector<CXCursor>> ExportExpansions;
	std::map<std::string, std::string> Macros;
	std::exception_ptr Error;

	static CXChildVisitResult Visit(CXCursor Cursor, CXCursor, CXClientData Data);
};
} // namespace Mod::HeaderTool
