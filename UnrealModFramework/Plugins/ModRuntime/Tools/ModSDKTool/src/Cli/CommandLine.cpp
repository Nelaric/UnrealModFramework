// Copyright (c) 2026 Nelaric Contributors
#include "CommandLine.h"

#include "ModSDKTool/src/Cli/EditorBuildCommand.h"
#include "ModSDKTool/src/Cli/SdkBuildCommands.h"
#include "ModSDKTool/src/Cli/SdkInspectCommand.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

int CommandLine::PrintHelp()
{
	std::cout << R"(ModSDKTool 0.1.0 - build a game-specific authoring SDK

Usage:
  ModSDKTool plan --request <SdkBuildRequest.json> [--output <plan.json>]
  ModSDKTool build --request <SdkBuildRequest.json> [--output <result.json>]
  ModSDKTool build-editor --package <directory>
                          --dotnet <host> --automation-tool <AutomationTool.dll>
                          --editor-modules <UnrealEditor.modules>
                          --architecture <x64|arm64> --mbt <ModBuildTool>
  ModSDKTool inspect --bundle <directory> [--output <inspection.json>]
  ModSDKTool --help | --version

Accepts UMF.SdkBuildRequest v2 and UMF.SdkContract v1 or v2.
Contracts must already contain complete, resolved projections. MHT scan reports
are rejected. Blueprint export belongs to ExportCodeGenerator.

The first SDK adapter supports Player/GameThread object getters and one-float
instance methods returning float. IDs, names, types and native symbols come
from the contract. No game private source is copied into the public SDK.

plan asks MBT to validate its inputs but does not compile or publish.
build calls MBT and UE AutomationTool, builds the SDK and independent developer
plugin, verifies their artifacts, then publishes a versioned bundle.
build-editor generates and packages the authoring plugin for the installed Editor.
Host library creation does not prove final game linkage or runtime binding.

Progress and child diagnostics use stderr; the final JSON uses stdout or
--output. Exit codes: 0 success, 1 input/build failure, 130 cancelled.
)";
	return std::cout ? 0 : 1;
}

struct CommandLine::Command
{
	std::string_view Name;
	std::initializer_list<std::string_view> Required;
	int (*Execute)(const Options&);
	bool bAcceptsOutput = true;
};

const CommandLine::Command& CommandLine::FindCommand(const std::string& Name)
{
	static const Command Commands[] = {
	    {"plan", {"--request"}, SdkBuildCommands::PlanSdkCommand},
	    {"build", {"--request"}, SdkBuildCommands::BuildSdkCommand},
	    {"inspect", {"--bundle"}, SdkInspectCommand::InspectSdkCommand},
	    {"build-editor",
	     {"--architecture", "--automation-tool", "--dotnet", "--editor-modules", "--mbt", "--package"},
	     EditorBuildCommand::BuildEditorCommand,
	     false},
	};

	const auto Command = std::ranges::find(Commands, std::string_view(Name), &Command::Name);
	if (Command == std::end(Commands))
	{
		throw std::runtime_error("Unknown command '" + Name + "'. Use --help.");
	}
	return *Command;
}

Options CommandLine::ParseOptions(const std::vector<std::string>& Arguments, const Command& Command)
{
	Options Options;
	for (std::size_t Index = 2; Index < Arguments.size(); ++Index)
	{
		const std::string& Key = Arguments[Index];
		const bool bRequired = std::ranges::find(Command.Required, std::string_view(Key)) != Command.Required.end();
		if ((!bRequired && (Key != "--output" || !Command.bAcceptsOutput)) || Options.contains(Key) ||
		    Index + 1 == Arguments.size() || Arguments[Index + 1].empty() || Arguments[Index + 1].starts_with("--"))
		{
			throw std::runtime_error("Unknown, duplicate, or missing-value option: " + Key);
		}
		Options.emplace(Key, Arguments[++Index]);
	}
	for (const auto Key : Command.Required)
	{
		if (!Options.contains(std::string(Key)))
		{
			throw std::runtime_error("Required option: " + std::string(Key));
		}
	}
	return Options;
}

int CommandLine::RunCommandLine(const std::vector<std::string>& Arguments)
{
	if (Arguments.size() == 1 || (Arguments.size() == 2 && (Arguments[1] == "--help" || Arguments[1] == "-h")))
	{
		return PrintHelp();
	}
	if (Arguments.size() == 2 && Arguments[1] == "--version")
	{
		std::cout << "ModSDKTool 0.1.0\n";
		return 0;
	}
	const auto& Command = FindCommand(Arguments.at(1));
	return Command.Execute(ParseOptions(Arguments, Command));
}
} // namespace Mod::SdkTool
