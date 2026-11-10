// Copyright (c) 2026 Nelaric Contributors
/** @file File.h */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class File
{
public:
	/// Reads a regular file as bytes and reports I/O failures.
	static std::string ReadFile(const fs::path& Path);
	/// Writes through a sibling temporary file and replaces the destination.
	static void WriteFileAtomically(const fs::path& Path, std::string_view Contents);
	/// Atomically replaces a file on the same filesystem.
	static void ReplaceFileAtomically(const fs::path& Source, const fs::path& Destination);
};
} // namespace Mod::BuildTool
