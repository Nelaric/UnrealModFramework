// Copyright (c) 2026 Nelaric Contributors
#include "CommandLine.h"

#include "ModHeaderTool/src/Cli/OptionsCheck.h"
#include "ModHeaderTool/src/Cli/ScanCommand.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Generation/Generation.h"

#include <algorithm>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
int CommandLine::RunCommandLine(const std::vector<std::string>& Arguments)
{
	const auto IsHelp = [](const std::string& Argument) { return Argument == "--help" || Argument == "-h"; };
	if (Arguments.size() <= 1 || (Arguments.size() == 2 && IsHelp(Arguments[1])))
	{
		PrintHelp(std::cout);
		return std::cout ? 0 : 1;
	}
	const auto& Command = Arguments[1];
	const bool bScan = Command == "scan";
	const bool bCheckOptions = Command == "check-options";
	const bool bGenerate = Command == "generate";
	if (!bScan && !bCheckOptions && !bGenerate)
	{
		throw std::runtime_error("Unknown command '" + Command + "'. Use --help for usage.");
	}
	if (Arguments.size() == 3 && IsHelp(Arguments[2]))
	{
		PrintHelp(std::cout);
		return std::cout ? 0 : 1;
	}
	if (Arguments.size() != (bGenerate ? 3u : 4u) ||
	    std::any_of(Arguments.begin() + 2, Arguments.end(),
	                [](const std::string& Argument) { return Argument.empty() || Argument.starts_with("--"); }))
	{
		throw std::runtime_error("Invalid arguments for '" + Command + "'. Use --help for usage.");
	}
	if (bGenerate)
	{
		Generation::Generate(Path::PathFromUtf8(Arguments[2]));
		return 0;
	}
	const auto Project = Path::PathFromUtf8(Arguments[2]);
	const auto Snapshot = Path::PathFromUtf8(Arguments[3]);
	return bScan ? ScanCommand::RunScan(Project, Snapshot) : OptionsCheck::CheckOptions(Project, Snapshot);
}

void CommandLine::PrintHelp(std::ostream& Output)
{
	Output << R"(ModHeaderTool - scan Mod export declarations

Usage:
  ModHeaderTool scan <file.uproject|directory> <metadata-options.json>
  ModHeaderTool generate <GenerationRequest.json>
  ModHeaderTool check-options <file.uproject|directory> <metadata-options.json>
  ModHeaderTool --help

generate resolves active API markers with libclang and an actual compilation
database. The build input pins modules, translation units, stable type identities,
Target context and analysis arguments. It emits SDK views, concrete host bridges,
compile verification, a contract and a complete manifest at fixed output paths.
Implementation .cpp files are compiled independently by MBT, never by UBT.
The UHT generation request supplies the build input, output directory, and
host context set. It also carries the resolved metadata snapshot, libclang,
and lifecycle source paths for the current target.
The backend supports public UObject instance methods on GameThread, scalar
and UObject pointer inputs, and scalar/void or World-scoped UObject pointer returns.
FullView selects parameter classes for public instance method generation.
Object returns use the explicitly selected ErrorIfNull projection. Other forms,
Event and Hook generation are rejected. No Blueprint assets are processed.

When a project directory is supplied, it must contain exactly one .uproject.
Scans Plugins, Source, and Directories from the optional <project>.modexpot JSON.
Relative configured directories resolve from the project directory.
scan writes its JSON report to stdout.
Macro parameters are parsed and validated against the shared metadata schema.
Choices come from the UE ModExportOptions commandlet, not Blueprint assets.
UHT stores its default snapshot at Intermediate/ModTools/ModHeaderTool/MetadataOptions.json.
Invalid metadata or stale catalogs produce "file"(line,column) diagnostics and
return 1 without writing a scan report.

check-options exits with 2 if the catalog is missing, stale, or from an older schema.

scan is a lexical scan of MOD_EXPORT_API, MOD_EXPORT_EVENT and MOD_EXPORT_HOOK.
Comments, literals and preprocessor directives are skipped. Conditional branches
are not evaluated. AST types, declaration binding and availability are not checked.
Line and column numbers are one-based; columns and offsets count UTF-8 bytes.
Exit codes: 0 = success, 1 = command-line, configuration, source or I/O error.
)";
}
} // namespace Mod::HeaderTool
