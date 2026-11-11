// Copyright (c) 2026 Nelaric Contributors
#include "CommandLine.h"

#include "ModBuildTool/src/Cli/HashCommand.h"
#include "ModBuildTool/src/Cli/HostCommands.h"
#include "ModBuildTool/src/Cli/InspectionCommands.h"
#include "ModBuildTool/src/Cli/UeCommands.h"
#include "ModBuildTool/src/Cli/Version.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
void CommandLine::PrintHelp()
{
	std::cout << "ModBuildTool " << Version::ToolVersion << R"( - build generated host archives independently of UBT

Usage:
  ModBuildTool plan --context <context.json> --generated-manifest <manifest.json>
  ModBuildTool build-host --context <context.json> --generated-manifest <manifest.json>
  ModBuildTool build-planned-host --context <context.json> --generated-manifest <manifest.json>
                                 --input-fingerprint <sha256>
  ModBuildTool verify-host --context <context.json> --generated-manifest <manifest.json>
  ModBuildTool finalize-host --context <context.json> --generated-manifest <manifest.json>
                            --link-receipt <link.json>
  ModBuildTool finalize-ue-target --contexts <contexts.json> --ue-target-receipt <target.target>
  ModBuildTool refresh-ue-watches --contexts <contexts.json>
  ModBuildTool hash-file --file <path>
  ModBuildTool hash-context --file <context.json>
  ModBuildTool inspect-archive --file <archive> --platform <Win64|Linux|Mac>
                              --architecture <x64|arm64> --anchor <symbol>
  ModBuildTool inspect-binary --file <image> --platform <Win64|Linux|Mac>
                             --architecture <x64|arm64> --kind <Executable|DynamicLibrary>
  ModBuildTool --help | --version

Requires UMF.ModHostContext v2 and UMF.GeneratedArtifacts v1, v2, or v3.
Generated v2 and v3 declare per-owner required symbols and public artifact roles.
The generated batch must be Complete, hash-checked, and match the exact context.
MHT scan reports are not generation manifests. A zero-export module still needs
a generated compilation unit defining its registration anchor.

Compiler and archiver paths, arguments, environment, and dependencies come from
the owning UE module. MBT never discovers an approximate host compiler setup.
Supported native archive formats: Win64 COFF, Linux ELF, macOS Mach-O;
architectures: x64 and arm64. LTO, PCH, unity, and C++ modules are not supported.
Library creation does not prove successful final game linkage or runtime binding.
finalize-host requires a v2 or v3 manifest, owningBinary, and a trusted successful link
record. It verifies identities and actual image bytes, not runtime behavior.

JSON results use stdout. Compiler diagnostics use stderr.
Exit codes: 0 = success, 1 = invalid input/build failure/stale verification.
)";
}

struct CommandLine::Command
{
	std::string_view Name;
	std::initializer_list<std::string_view> Required;
	int (*Execute)(const CommandArguments&);
};

const CommandLine::Command& CommandLine::FindCommand(const std::string& Name)
{
	static const Command Commands[] = {
	    {"plan", {"--context", "--generated-manifest"}, HostCommands::RunHostCommand},
	    {"build-host", {"--context", "--generated-manifest"}, HostCommands::RunHostCommand},
	    {"build-planned-host",
	     {"--context", "--generated-manifest", "--input-fingerprint"},
	     HostCommands::RunHostCommand},
	    {"verify-host", {"--context", "--generated-manifest"}, HostCommands::RunHostCommand},
	    {"finalize-host", {"--context", "--generated-manifest", "--link-receipt"}, HostCommands::RunHostCommand},
	    {"finalize-ue-target", {"--contexts", "--ue-target-receipt"}, UeCommands::FinalizeTarget},
	    {"refresh-ue-watches", {"--contexts"}, UeCommands::RefreshWatches},
	    {"hash-file", {"--file"}, HashCommand::HashInput},
	    {"hash-context", {"--file"}, HashCommand::HashInput},
	    {"inspect-archive", {"--file", "--platform", "--architecture", "--anchor"}, InspectionCommands::InspectArchive},
	    {"inspect-binary", {"--file", "--platform", "--architecture", "--kind"}, InspectionCommands::InspectBinary},
	};

	const auto Route = std::ranges::find(Commands, std::string_view(Name), &Command::Name);
	if (Route == std::end(Commands))
	{
		throw std::runtime_error("Unknown command '" + Name + "'. Use --help.");
	}
	return *Route;
}

CommandArguments CommandLine::ParseOptions(const std::vector<std::string>& Arguments, const Command& Route)
{
	const auto& Name = Arguments.at(1);
	CommandArguments Command{Name, {}};
	for (std::size_t Index = 2; Index < Arguments.size(); ++Index)
	{
		const std::string& Option = Arguments[Index];
		const bool bAllowed = std::ranges::find(Route.Required, std::string_view(Option)) != Route.Required.end();
		const bool bHasValue =
		    Index + 1 < Arguments.size() && !Arguments[Index + 1].empty() && !Arguments[Index + 1].starts_with("--");
		if (!bAllowed || !bHasValue || Command.Options.contains(Option))
		{
			throw std::runtime_error("Unknown, duplicate, or missing-value option: " + Option);
		}
		Command.Options.emplace(Option, Arguments[++Index]);
	}
	for (const auto Required : Route.Required)
	{
		if (!Command.Options.contains(std::string(Required)))
		{
			throw std::runtime_error(Name + " requires " + std::string(Required) + ".");
		}
	}
	return Command;
}

int CommandLine::RunCommandLine(const std::vector<std::string>& Arguments)
{
	if (Arguments.size() == 1 || (Arguments.size() == 2 && (Arguments[1] == "--help" || Arguments[1] == "-h")))
	{
		PrintHelp();
		return 0;
	}
	if (Arguments.size() == 2 && Arguments[1] == "--version")
	{
		std::cout << "ModBuildTool " << Version::ToolVersion << '\n';
		return 0;
	}
	const auto& Route = FindCommand(Arguments.at(1));
	return Route.Execute(ParseOptions(Arguments, Route));
}
} // namespace Mod::BuildTool
