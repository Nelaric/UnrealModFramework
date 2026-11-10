// Copyright (c) 2026 Nelaric Contributors
/** @file CursorSource.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Analysis/Location.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <clang-c/Index.h>
#include <filesystem>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class CursorSource
{
public:
	static std::string Key(const fs::path& Path);
	static Location ReadLocation(const ClangLibrary& Clang, CXSourceLocation Source, bool bResolvePath = false);
	static std::string QualifiedName(const ClangLibrary& Clang, CXCursor Cursor);
	[[noreturn]] static void Fail(const ExportMacro& Macro, const std::string& Reason);
};
} // namespace Mod::HeaderTool
