// Copyright (c) 2026 Nelaric Contributors
/** @file FileSystem.h */
#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

/// Converts a native path to UTF-8.
std::string PathToUtf8(const fs::path& Path);
/// Converts UTF-8 to a native path.
fs::path PathFromUtf8(std::string_view Text);
/// Reads a regular file as bytes and reports I/O failures.
std::string ReadFile(const fs::path& Path);
/// Resolves a path relative to an explicit input directory.
fs::path ResolvePath(const fs::path& Base, std::string_view Text);
/// Requires a generated file to remain inside its declared root.
fs::path ResolveGeneratedPath(const fs::path& Root, std::string_view Text);
/// Writes through a sibling temporary file and replaces the destination.
void WriteFileAtomically(const fs::path& Path, std::string_view Contents);
/// Atomically replaces a file on the same filesystem.
void ReplaceFileAtomically(const fs::path& Source, const fs::path& Destination);
/// Returns the actual executable, independently of argv or PATH lookup.
fs::path ExecutablePath();
/// Returns a process-local unique suffix for owned temporary paths.
std::string UniqueSuffix();

/// Holds an OS lock until destruction, including abnormal process exit.
class FBuildLock
{
public:
	/// Fails immediately if another writer owns this output's lock.
	explicit FBuildLock(const fs::path& Output);
	/// Uses a lock path with the given suffix.
	FBuildLock(const fs::path& Output, std::string_view Suffix);
	/// Releases the operating-system lock.
	~FBuildLock();
	FBuildLock(const FBuildLock&) = delete;
	FBuildLock& operator=(const FBuildLock&) = delete;

private:
	std::intptr_t Handle = -1;
};
} // namespace Mod::BuildTool
