// Copyright (c) 2026 Nelaric Contributors
/** @file Source.h */
#pragma once

#include "ModHeaderTool/src/Analysis/ClangLibrary.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <clang-c/Index.h>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct Source
{
	struct Range
	{
		std::size_t Begin;
		std::size_t End;
	};

	Source(ClangLibrary& Clang, CXTranslationUnit Tu, CXFile File, const fs::path& Path);
	std::optional<ExportMacro> ReadMacro(ClangLibrary& Clang, CXCursor Expansion) const;

	fs::path Path;
	std::string Text;
	std::string TextWithoutConditionalDirectives;
	std::vector<Range> ConditionalGroups;

private:
	void MaskConditionalDirectives(ClangLibrary& Clang, CXTranslationUnit Tu);
	std::optional<std::size_t> Offset(ClangLibrary& Clang, CXSourceLocation Location) const;
	std::optional<Range> Offsets(ClangLibrary& Clang, CXSourceRange Extent) const;

	CXFile File;
};
} // namespace Mod::HeaderTool
