// Copyright (c) 2026 Nelaric Contributors
/** @file CommandLineOptions.h */
#pragma once

#include <filesystem>

namespace Mod::HeaderTool
{
/// Parsed scan/generate options and help requests.
struct CommandLineOptions
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
	/// Optional source directory of the global Mod lifecycle declarations.
	std::filesystem::path LifecycleSource;
	/// Selects source generation instead of the lexical scan.
	bool bGenerate = false;
	/// Checks the UE metadata snapshot without scanning source or generating files.
	bool bCheckOptions = false;
	/// Requests help instead of scanning.
	bool bHelp = false;
};
} // namespace Mod::HeaderTool
