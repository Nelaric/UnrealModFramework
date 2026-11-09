// Copyright (c) 2026 Nelaric Contributors
/** @file MacroScanner.h */
#pragma once

#include "ModHeaderTool/FileSystem.h"
#include "ModHeaderTool/Metadata.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
/// A lexical macro invocation with its original source spelling.
struct FExportMacro
{
	/// Export macro identifier.
	std::string Name;
	/// Canonical source file path.
	fs::path File;
	/// One-based physical source line.
	std::size_t Line = 0;
	/// One-based UTF-8 byte column.
	std::size_t Column = 0;
	/// Zero-based byte offset, including any file BOM.
	std::size_t Offset = 0;
	/// Byte length through the closing parenthesis.
	std::size_t Length = 0;
	/// Unchanged source text between the parentheses.
	std::string Arguments;
	/// Byte position immediately after the opening parenthesis.
	std::size_t ArgumentsOffset = 0;
	/// Parsed and validated fields, including schema defaults.
	std::vector<FMetadataField> Fields;
};

/// Collects invocations without evaluating conditional compilation.
std::vector<FExportMacro> ScanFile(const fs::path& Path);
/// Collects invocations from an existing UTF-8 source buffer.
std::vector<FExportMacro> ScanSource(const fs::path& Path, const std::string& Source);
} // namespace Mod::HeaderTool
