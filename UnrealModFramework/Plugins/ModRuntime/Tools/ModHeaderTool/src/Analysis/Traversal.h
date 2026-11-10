// Copyright (c) 2026 Nelaric Contributors
/** @file Traversal.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Analysis/Source.h"

#include <clang-c/Index.h>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
struct Traversal
{
	ClangLibrary& Clang;
	CXTranslationUnit Tu;
	std::map<std::string, Source>& Sources;
	std::vector<CXCursor> Methods;
	std::set<std::string> Active;
	std::map<std::string, std::string> Macros;
	std::exception_ptr Error;

	static CXChildVisitResult Visit(CXCursor Cursor, CXCursor, CXClientData Data);
};
} // namespace Mod::HeaderTool
