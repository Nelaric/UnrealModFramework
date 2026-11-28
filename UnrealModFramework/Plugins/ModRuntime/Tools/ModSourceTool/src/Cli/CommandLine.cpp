// Copyright (c) 2026 Nelaric Contributors
#include "CommandLine.h"

#include "Common/Files/Path.h"
#include "ModSourceTool/src/Analysis/HookSourceWriter.h"
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
  ModSourceTool generate-host-link <owner-module> <output-header>
  ModSourceTool rewrite-hooks <HookSourceRequest.json>
  ModSourceTool --help | --version

Traces declared failure returns and escaping C++ throws through visible definitions.
Requires a compiler database with complete non-PCH arguments for the selected Target.
The UMF.SourceAnalysisRequest v2 document groups AST-discovered members by their actual
translation units and includes the toolchain information. The report contains bound
framework codes and explicit authored business codes.
generate-host-link writes the Win64 x64 MSVC header that retains an MHT host archive.
rewrite-hooks reports the rewritten game translation units using their real compiler commands.
)";
	return std::cout ? 0 : 1;
}

int CommandLine::RunHostLinkCommand(const std::vector<std::string>& Arguments)
{
	if (Arguments.size() < 4 || Arguments[2].empty() || Arguments[3].empty())
	{
		throw std::runtime_error("Usage: ModSourceTool generate-host-link <owner-module> <output-header>");
	}
	HostLinkHeader::GenerateHostLink(Arguments[2], Arguments[3], std::span(Arguments).subspan(4));
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
	if (Arguments[1] == "rewrite-hooks" && Arguments.size() == 3)
	{
		HookSourceWriter::Rewrite(Mod::BuildTool::Path::PathFromUtf8(Arguments[2]));
		return std::cout ? 0 : 1;
	}
	if (Arguments[1] != "analyze-errors")
	{
		throw std::runtime_error("Unknown command. Use --help.");
	}
	if (Arguments.size() != 3 || Arguments[2].empty() || Arguments[2].starts_with("--"))
	{
		throw std::runtime_error("Expected one source analysis request path. Use --help for usage.");
	}
	const auto Request = Mod::BuildTool::Path::PathFromUtf8(Arguments[2]);
	return AnalysisCommand::RunAnalysis(AnalysisOptions::Read(Request));
}
} // namespace Mod::SourceTool
