// Copyright (c) 2026 Nelaric Contributors
/** @file FullViewSelection.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Analysis/FullView.h"
#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <clang-c/Index.h>
#include <map>
#include <set>
#include <string>

namespace Mod::HeaderTool
{
class FullViewSelection
{
public:
	static void SelectFullViews(GenerationModel& Model, ClangLibrary& Clang, CXCursor Method, const API& Exported,
	                            std::map<std::string, FullView>& FullViews);

private:
	static std::string MemberKey(ClangLibrary& Clang, CXCursor Method);
	static void CollectPublicMembers(ClangLibrary& Clang, CXCursor Record, std::map<std::string, CXCursor>& Members,
	                                 std::set<std::string>& Visited);
};
} // namespace Mod::HeaderTool
