// Copyright (c) 2026 Nelaric Contributors
#include "CommandLine.h"

#include "Common/Files/Path.h"
#include "ModSourceTool/src/Cli/AnalysisCommand.h"
#include "ModSourceTool/src/Cli/AnalysisOptions.h"
#include "ModSourceTool/src/Cli/HostLinkHeader.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
int CommandLine::PrintHelp()
{
	std::cout << R"(Usage:
  ModSourceTool analyze-errors <SourceAnalysisRequest.json>
  ModSourceTool update-catalog <SourceAnalysisRequest.json>
  ModSourceTool generate-host-link <owner-module> <output-header>
  ModSourceTool --help | --version

Traces configured failure returns and escaping C++ throws through visible definitions.
For v2, failureReturn accepts a boolean, an integer, or null for a pointer.
Omit failureReturn to analyze exceptions alone.
Omit signature only when the qualified function name identifies one definition.
Requires a compiler database with complete non-PCH arguments for the selected Target.
The UMF.SourceAnalysisRequest v1 document supplies compilationDatabase, target,
catalog, member, libclang, resourceDirectory and an analysisArguments array.
Both commands report JSON on stdout. update-catalog records new classifications
and assigns their stable codes.
generate-host-link writes the Win64 x64 MSVC header that retains an MHT host archive.
Keep existing reason keys and codes when changing their guard spellings.
)";
	return std::cout ? 0 : 1;
}

int CommandLine::RunHostLinkCommand(const std::vector<std::string>& Arguments)
{
	if (Arguments.size() != 4 || Arguments[2].empty() || Arguments[3].empty())
	{
		throw std::runtime_error("Usage: ModSourceTool generate-host-link <owner-module> <output-header>");
	}
	HostLinkHeader::GenerateHostLink(Arguments[2], Arguments[3]);
	return 0;
}

int CommandLine::RunCommandLine(const std::vector<std::string>& Arguments)
{
	if (Arguments.size() <= 1 || (Arguments.size() == 2 && (Arguments[1] == "--help" || Arguments[1] == "-h")))
	{
		return PrintHelp();
	}
	if (Arguments.size() == 2 && Arguments[1] == "--version")
	{
		std::cout << "ModSourceTool 0.1.0\n";
		return std::cout ? 0 : 1;
	}
	if (Arguments[1] == "generate-host-link")
	{
		return RunHostLinkCommand(Arguments);
	}
	const bool bUpdateCatalog = Arguments[1] == "update-catalog";
	if (Arguments[1] != "analyze-errors" && !bUpdateCatalog)
	{
		throw std::runtime_error("Unknown command. Use --help.");
	}
	if (Arguments.size() != 3 || Arguments[2].empty() || Arguments[2].starts_with("--"))
	{
		throw std::runtime_error("Expected one source analysis request path. Use --help for usage.");
	}
	const auto Request = Mod::BuildTool::Path::PathFromUtf8(Arguments[2]);
	return AnalysisCommand::RunAnalysis(AnalysisOptions::Read(Request), bUpdateCatalog);
}
} // namespace Mod::SourceTool
