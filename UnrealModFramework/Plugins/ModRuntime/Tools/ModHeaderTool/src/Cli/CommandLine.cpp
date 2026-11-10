// Copyright (c) 2026 Nelaric Contributors
#include "CommandLine.h"

#include "ModHeaderTool/src/Files/Path.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

CommandLineOptions CommandLine::ParseArguments(const std::vector<std::string>& Arguments)
{
	CommandLineOptions Result;
	std::map<std::string, fs::path*> Paths{{"--project", &Result.Project},
	                                       {"--output", &Result.Output},
	                                       {"--options", &Result.Options},
	                                       {"--build-input", &Result.BuildInput},
	                                       {"--output-dir", &Result.OutputDirectory},
	                                       {"--libclang", &Result.ClangLibrary},
	                                       {"--host-contexts", &Result.HostContexts},
	                                       {"--lifecycle-source", &Result.LifecycleSource}};
	std::set<std::string> Supplied;
	Result.bGenerate = Arguments.size() > 1 && Arguments[1] == "generate";
	Result.bCheckOptions = Arguments.size() > 1 && Arguments[1] == "check-options";
	std::size_t Index =
	    Arguments.size() > 1 && (Arguments[1] == "scan" || Result.bGenerate || Result.bCheckOptions) ? 2 : 1;
	for (; Index < Arguments.size(); ++Index)
	{
		const std::string& Argument = Arguments[Index];
		if (Argument == "--help" || Argument == "-h")
		{
			Result.bHelp = true;
		}
		else if (Paths.contains(Argument))
		{
			if (!Supplied.insert(Argument).second || Index + 1 == Arguments.size() || Arguments[Index + 1].empty() ||
			    Arguments[Index + 1].starts_with("--"))
			{
				throw std::runtime_error(Argument + " requires exactly one path argument.");
			}
			*Paths.at(Argument) = Path::PathFromUtf8(Arguments[++Index]);
		}
		else
		{
			throw std::runtime_error("Unknown argument '" + Argument + "'. Use --help for usage.");
		}
	}
	if (!Result.bHelp)
	{
		if (Result.bCheckOptions && Supplied.contains("--output"))
		{
			throw std::runtime_error("check-options does not write an output file.");
		}
		if (Result.bGenerate &&
		    (Result.BuildInput.empty() || Result.OutputDirectory.empty() || Result.HostContexts.empty() ||
		     Supplied.contains("--project") || Supplied.contains("--output")))
		{
			throw std::runtime_error("generate requires --build-input, --output-dir and --host-contexts. The project "
			                         "comes from the build input.");
		}
		if (!Result.bGenerate &&
		    (!Result.BuildInput.empty() || !Result.OutputDirectory.empty() || !Result.ClangLibrary.empty() ||
		     !Result.HostContexts.empty() || !Result.LifecycleSource.empty()))
		{
			throw std::runtime_error(
			    "--build-input, --output-dir, --libclang, --host-contexts and --lifecycle-source require generate.");
		}
	}
	return Result;
}

void CommandLine::PrintHelp(std::ostream& Output)
{
	Output << R"(ModHeaderTool - scan Mod export declarations

Usage:
  ModHeaderTool [scan] [--project <file.uproject|directory>] [--output <file.json>]
                      [--options <metadata-options.json>]
  ModHeaderTool generate --build-input <target.json> --output-dir <directory>
                         [--options <metadata-options.json>] [--libclang <library>]
                         --host-contexts <contexts.json> [--lifecycle-source <directory>]
  ModHeaderTool check-options [--project <file.uproject|directory>] [--options <metadata-options.json>]

generate resolves active API markers with libclang and an actual compilation
database. The build input pins modules, translation units, stable type identities,
Target context and analysis arguments. It emits SDK views, concrete host bridges,
compile verification, a contract and a complete manifest at fixed output paths.
Implementation .cpp files are compiled independently by MBT, never by UBT.
--host-contexts pins each owner's actual MBT producer context in the manifest.
--lifecycle-source exports the host's lifecycle declarations for SDK generation.
The backend supports public UObject instance methods on GameThread, scalar
and UObject pointer inputs, and scalar/void or World-scoped UObject pointer returns.
FullView selects parameter classes for public instance method generation.
Object returns use the explicitly selected ErrorIfNull projection. Other forms,
Event and Hook generation are rejected. No Blueprint assets are processed.

The project defaults to the current directory, which must contain one .uproject.
Scans Plugins, Source, and Directories from the optional <project>.modexpot JSON.
Relative configured directories resolve from the project directory.
Outputs JSON to stdout unless --output is supplied.
Macro parameters are parsed and validated against the shared metadata schema.
Choices come from the UE ModExportOptions commandlet, not Blueprint assets.
The default catalog is <project>/Intermediate/ModTools/ModHeaderTool/MetadataOptions.json.
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
