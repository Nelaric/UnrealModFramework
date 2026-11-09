// Copyright (c) 2026 Nelaric Contributors
/** @file CommandLine.h */
#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
/// Parsed scan/generate options and help requests.
struct FCommandLineOptions
{
	/// Project file or directory, defaulting to the current directory.
	std::filesystem::path Project = std::filesystem::current_path();
	/// Output destination; an empty path selects stdout.
	std::filesystem::path Output;
	/// Optional authoritative UE metadata option catalog.
	std::filesystem::path Options;
	/// Explicit Target, compiler commands, modules, and type identities.
	std::filesystem::path BuildInput;
	/// Fixed output directory replaced by each successful generation.
	std::filesystem::path OutputDirectory;
	/// Optional path to the locked libclang shared library.
	std::filesystem::path ClangLibrary;
	/// Optional exact per-owner MBT contexts supplied by the build adapter.
	std::filesystem::path HostContexts;
	/// Selects source generation instead of the lexical scan.
	bool bGenerate = false;
	/// Requests help instead of scanning.
	bool bHelp = false;
};

/// Parses command-line arguments, including the executable name.
FCommandLineOptions ParseArguments(const std::vector<std::string>& Arguments);

/// Writes command-line usage without accessing project files.
void PrintHelp(std::ostream& Output);
} // namespace Mod::HeaderTool
