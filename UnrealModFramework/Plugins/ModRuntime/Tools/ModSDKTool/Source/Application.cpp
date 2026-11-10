// Copyright (c) 2026 Nelaric Contributors
#include "ModSDKTool/Application.h"

#include "Sdk.h"

#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace Mod::SdkTool
{
namespace
{
int Run(const std::vector<std::string>& Arguments)
{
	fs::path Output;
	bool bResultValidated = false;
	try
	{
		if (Arguments.size() == 1 || (Arguments.size() == 2 && (Arguments[1] == "--help" || Arguments[1] == "-h")))
		{
			std::cout << R"(ModSDKTool 0.1.0 - build a game-specific authoring SDK

Usage:
  ModSDKTool plan --request <SdkBuildRequest.json> [--output <plan.json>]
  ModSDKTool build --request <SdkBuildRequest.json> [--output <result.json>]
  ModSDKTool build-editor --plugin <plugin.uplugin> --package <directory>
                          --dotnet <host> --automation-tool <AutomationTool.dll>
                          --editor-modules <UnrealEditor.modules>
                          --architecture <x64|arm64> --mbt <ModBuildTool>
  ModSDKTool inspect --bundle <directory> [--output <inspection.json>]
  ModSDKTool --help | --version

Accepts UMF.SdkBuildRequest and UMF.SdkContract schemaVersion 1.
Contracts must already contain complete, resolved projections. MHT scan reports
are rejected. Blueprint export belongs to ExportCodeGenerator.

The first SDK adapter supports Player/GameThread object getters and one-float
instance methods returning float. IDs, names, types and native symbols come
from the contract. No game private source is copied into the public SDK.

plan asks MBT to validate its inputs but does not compile or publish.
build calls MBT and UE AutomationTool, builds the SDK and independent developer
plugin, verifies their artifacts, then publishes a versioned bundle.
build-editor packages the independent authoring plugin for the installed Editor.
Host library creation does not prove final game linkage or runtime binding.

Progress and child diagnostics use stderr; the final JSON uses stdout or
--output. Exit codes: 0 success, 1 input/build failure, 130 cancelled.
)";
			return std::cout ? 0 : 1;
		}
		if (Arguments.size() == 2 && Arguments[1] == "--version")
		{
			std::cout << "ModSDKTool 0.1.0\n";
			return 0;
		}
		const std::string Command = Arguments.at(1);
		std::set<std::string> RequiredOptions;
		if (Command == "plan" || Command == "build")
		{
			RequiredOptions = {"--request"};
		}
		else if (Command == "inspect")
		{
			RequiredOptions = {"--bundle"};
		}
		else if (Command == "build-editor")
		{
			RequiredOptions = {"--plugin",         "--package",      "--dotnet", "--automation-tool",
			                   "--editor-modules", "--architecture", "--mbt"};
		}
		else
		{
			throw std::runtime_error("Unknown command '" + Command + "'. Use --help.");
		}
		std::map<std::string, std::string> Options;
		for (std::size_t Index = 2; Index < Arguments.size(); ++Index)
		{
			const std::string& Key = Arguments[Index];
			if ((!RequiredOptions.contains(Key) && (Key != "--output" || Command == "build-editor")) ||
			    Options.contains(Key) || Index + 1 == Arguments.size() || Arguments[Index + 1].empty() ||
			    Arguments[Index + 1].starts_with("--"))
			{
				throw std::runtime_error("Unknown, duplicate, or missing-value option: " + Key);
			}
			Options.emplace(Key, Arguments[++Index]);
		}
		for (const std::string& Key : RequiredOptions)
		{
			if (!Options.contains(Key))
			{
				throw std::runtime_error("Required option: " + Key);
			}
		}
		const auto OptionPath = [&Options](const char* Key) { return Mod::BuildTool::PathFromUtf8(Options.at(Key)); };
		if (Command == "build-editor")
		{
			const std::string& Architecture = Options.at("--architecture");
			if (Architecture != "x64" && Architecture != "arm64")
			{
				throw std::runtime_error("--architecture must be x64 or arm64.");
			}
			const FEditorBuild Editor{
			    .Dotnet = fs::canonical(OptionPath("--dotnet")),
			    .AutomationTool = fs::canonical(OptionPath("--automation-tool")),
			    .Mbt = fs::canonical(OptionPath("--mbt")),
#ifdef _WIN32
			    .Platform = "Win64",
#elif defined(__APPLE__)
			    .Platform = "Mac",
#else
			    .Platform = "Linux",
#endif
			    .Architecture = Architecture,
			    .BuildId = Text(ReadJson(OptionPath("--editor-modules")), "BuildId"),
			};
			BuildEditorPlugin(Editor, OptionPath("--plugin"), OptionPath("--package"));
			return 0;
		}
		if (Options.contains("--output"))
		{
			Output = fs::weakly_canonical(fs::absolute(OptionPath("--output")));
		}
		FJson Result;
		if (Command == "inspect")
		{
			const fs::path Bundle = fs::canonical(OptionPath("--bundle"));
			if (!Output.empty())
			{
				if (IsInside(Output, Bundle))
				{
					throw std::runtime_error("Inspection output must be outside the inspected bundle.");
				}
			}
			bResultValidated = true;
			Result = InspectSdk(Bundle);
		}
		else
		{
			FSdkRequest Request = ReadSdkRequest(OptionPath("--request"));
			ValidateResultOutput(Request, Output);
			bResultValidated = true;
			Result = Command == "plan" ? PlanSdk(Request) : BuildSdk(Request);
		}
		if (Output.empty())
		{
			std::cout << Result.dump(2) << '\n';
			return std::cout ? 0 : 1;
		}
		Mod::BuildTool::WriteFileAtomically(Output, Result.dump(2) + "\n");
		return 0;
	}
	catch (const std::exception& Error)
	{
		const bool bCancelled = std::string_view(Error.what()) == "SDK build cancelled.";
		const FJson Failure = {{"format", "UMF.SdkResult"},
		                       {"schemaVersion", 1},
		                       {"status", bCancelled ? "Cancelled" : "Failed"},
		                       {"message", Error.what()}};
		std::cerr << "ModSDKTool: error: " << Error.what() << '\n';
		if (bResultValidated && !Output.empty())
		{
			try
			{
				Mod::BuildTool::WriteFileAtomically(
				    Output, Failure.dump(2, ' ', false, FJson::error_handler_t::replace) + "\n");
			}
			catch (const std::exception& WriteError)
			{
				std::cerr << "ModSDKTool: result: " << WriteError.what() << '\n';
			}
		}
		return bCancelled ? 130 : 1;
	}
}
} // namespace

void ReportStage(std::string_view Stage, std::string_view Message)
{
	std::cerr
	    << FJson({{"format", "UMF.SdkProgress"}, {"schemaVersion", 1}, {"stage", Stage}, {"message", Message}}).dump()
	    << '\n';
}

int RunApplication(int Count, char* Arguments[])
{
	return Run(std::vector<std::string>(Arguments, Arguments + Count));
}

#ifdef _WIN32
int RunApplication(int Count, wchar_t* Arguments[])
{
	std::vector<std::string> Converted;
	for (int Index = 0; Index < Count; ++Index)
	{
		Converted.push_back(Mod::BuildTool::PathToUtf8(fs::path(Arguments[Index])));
	}
	return Run(Converted);
}
#endif
} // namespace Mod::SdkTool
