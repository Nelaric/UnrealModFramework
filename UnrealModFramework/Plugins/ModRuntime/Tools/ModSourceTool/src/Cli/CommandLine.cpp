// Copyright (c) 2026 Nelaric Contributors
#include "CommandLine.h"

#include "ModSourceTool/src/Cli/AnalysisCommand.h"
#include "ModSourceTool/src/Cli/HostLinkHeader.h"

#include <cstddef>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Mod::SourceTool
{
int CommandLine::PrintHelp()
{
	std::cout << R"(Usage:
  ModSourceTool analyze-errors --compdb <compile_commands.json> --target <UBT target>
    --catalog <catalog.json> --member <member ID> --libclang <library>
    --resource-dir <Clang resource directory> [--analysis-arg <argument>]...
    [--output <new report.json>] [--update-catalog]
  ModSourceTool generate-host-link <owner-module> <output-header>
  ModSourceTool --help | --version

Traces configured failure returns and escaping C++ throws through visible definitions.
For v2, failureReturn accepts a boolean, an integer, or null for a pointer.
Omit failureReturn to analyze exceptions alone.
Omit signature only when the qualified function name identifies one definition.
Requires a compiler database with complete non-PCH arguments for the selected Target.
Reports JSON on stdout or in --output. --analysis-arg appends a Clang argument.
--update-catalog records new classifications and assigns their stable codes.
generate-host-link writes the Win64 x64 MSVC header that retains an MHT host archive.
Keep existing reason keys and codes when changing their guard spellings.
)";
	return std::cout ? 0 : 1;
}

AnalysisOptions CommandLine::ParseAnalysisOptions(const std::vector<std::string>& Arguments)
{
	const std::set<std::string> Required{"--catalog", "--compdb",       "--libclang",
	                                     "--member",  "--resource-dir", "--target"};
	std::map<std::string, std::string> Options;
	std::vector<std::string> AnalysisArguments;
	bool bUpdate = false;
	for (std::size_t Index = 2; Index < Arguments.size(); ++Index)
	{
		const auto& Name = Arguments[Index];
		if (Name == "--update-catalog" && !bUpdate)
		{
			bUpdate = true;
			continue;
		}
		if (Name == "--analysis-arg" && Index + 1 < Arguments.size() && !Arguments[Index + 1].empty())
		{
			AnalysisArguments.push_back(Arguments[++Index]);
			continue;
		}
		if ((!Required.contains(Name) && Name != "--output") || Index + 1 == Arguments.size() ||
		    Arguments[Index + 1].empty() || Arguments[Index + 1].starts_with("--") ||
		    !Options.emplace(Name, Arguments[Index + 1]).second)
		{
			throw std::runtime_error("Unknown, duplicate, or missing-value option: " + Name);
		}
		++Index;
	}
	for (const auto& Name : Required)
	{
		if (!Options.contains(Name))
		{
			throw std::runtime_error("Required option: " + Name);
		}
	}
	return {
	    .Catalog = std::move(Options.at("--catalog")),
	    .MemberId = std::move(Options.at("--member")),
	    .CompilationDatabase = std::move(Options.at("--compdb")),
	    .Target = std::move(Options.at("--target")),
	    .ClangLibrary = std::move(Options.at("--libclang")),
	    .ResourceDirectory = std::move(Options.at("--resource-dir")),
	    .Output = Options.contains("--output") ? std::move(Options.at("--output")) : std::string{},
	    .AnalysisArguments = std::move(AnalysisArguments),
	    .bUpdateCatalog = bUpdate,
	};
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
	if (Arguments[1] != "analyze-errors")
	{
		throw std::runtime_error("Unknown command. Use --help.");
	}
	return AnalysisCommand::RunAnalysis(ParseAnalysisOptions(Arguments));
}
} // namespace Mod::SourceTool
