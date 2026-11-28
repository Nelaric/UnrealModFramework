// Copyright (c) 2026 Nelaric Contributors
#include "UeCommands.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/Unreal/MetadataBundle.h"
#include "ModBuildTool/src/Build/Unreal/UeBuildAdapter.h"
#include "ModBuildTool/src/Build/Unreal/UeHookBuild.h"
#include "ModBuildTool/src/Build/Unreal/UeInvocation.h"

#include <iostream>
#include <set>
#include <stdexcept>

namespace Mod::BuildTool
{
namespace
{
UeHookTarget ReadTarget(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	auto Target = UeHookTarget{.Engine = Path::PathFromUtf8(Options.at("--engine")),
	                           .Project = Path::PathFromUtf8(Options.at("--project")),
	                           .Target = Options.at("--target"),
	                           .Platform = Options.at("--platform"),
	                           .Configuration = "Development",
	                           .Architecture = Options.at("--architecture"),
	                           .Mode = {},
	                           .Metadata = {},
	                           .SourceCommit = {},
	                           .EngineCommit = {}};
	for (const auto* Name : {&Target.Target, &Target.Platform, &Target.Architecture})
	{
		if (Name->empty() ||
		    Name->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != Name->npos)
		{
			throw std::runtime_error("Invalid Unreal target identifier.");
		}
	}
	return Target;
}

void VerifyAutomation(const fs::path& Report)
{
	const auto Log = File::ReadFile(Report / "editor.log");
	const auto Result = ContextDocument::ReadJson(Report / "index.json");
	const auto& Tests = Result.at("tests");
	const auto Passed = Result.at("succeeded").get<int>() + Result.at("succeededWithWarnings").get<int>();
	if (Passed <= 0 || !Tests.is_array() || Tests.size() != static_cast<std::size_t>(Passed) ||
	    Result.at("failed") != 0 || Result.at("notRun") != 0 || Result.value("inProcess", 0) != 0 ||
	    Log.find("**** TEST COMPLETE. EXIT CODE: 0 ****") == Log.npos)
	{
		throw std::runtime_error("Automation had failures, incomplete execution, or no discovered Nelaric tests.");
	}
	std::set<std::string> Completed;
	for (const auto& Test : Tests)
	{
		auto Name = ContextDocument::Text(Test, "fullTestPath");
		if (!Name.starts_with("Nelaric.") || Test.at("state") != "Success" || !Completed.insert(std::move(Name)).second)
		{
			throw std::runtime_error(
			    "Automation did not report a successful execution for every discovered Nelaric test.");
		}
	}
	for (const auto* Expected :
	     {"Nelaric.UnrealModFramework.Host.ChangeHealth", "Nelaric.UnrealModFramework.Host.ChangeHealthDispatch",
	      "Nelaric.UnrealModFramework.Host.BlueprintExport", "Nelaric.UnrealModFramework.Host.EventWorldLifecycle",
	      "Nelaric.UnrealModFramework.Save.PersistentReferences"})
	{
		if (!Completed.contains(Expected))
		{
			throw std::runtime_error("Expected automation test was not discovered: " + std::string(Expected));
		}
	}
	std::cout << "UE automation validated " << Completed.size() << " completed Nelaric tests.\n";
}
} // namespace

int UeCommands::PrepareMetadata(const CommandArguments& Command)
{
	auto Target = ReadTarget(Command);
	Target.Mode = "bootstrap";
	Target.Metadata = Path::PathFromUtf8(Command.Options.at("--output"));
	Target.SourceCommit = Command.Options.at("--commit");
	Target.EngineCommit = Command.Options.at("--engine-commit");
	const auto Phase = Command.Name == "prepare-editor-bootstrap" ? MetadataBundle::EPhase::Bootstrap
	                   : Command.Name == "export-metadata"        ? MetadataBundle::EPhase::Export
	                                                              : MetadataBundle::EPhase::Both;
	if (Phase != MetadataBundle::EPhase::Both)
	{
		Target.Configuration = Command.Options.at("--configuration");
	}
	MetadataBundle::Prepare(Target, Phase);
	return 0;
}

int UeCommands::ValidateMetadata(const CommandArguments& Command)
{
	auto Target = ReadTarget(Command);
	Target.Configuration = Command.Options.at("--configuration");
	Target.Metadata = Path::PathFromUtf8(Command.Options.at("--metadata"));
	Target.SourceCommit = Command.Options.at("--commit");
	Target.EngineCommit = Command.Options.at("--engine-commit");
	if (Command.Name == "import-ue-metadata")
	{
		MetadataBundle::Consume(Target, UeHookBuild::MetadataInputs(Target));
	}
	else
	{
		MetadataBundle::Validate(Target);
	}
	return 0;
}

int UeCommands::ExportUeActions(const CommandArguments& Command)
{
	auto Target = ReadTarget(Command);
	Target.Configuration = Command.Options.at("--configuration");
	const auto Phase = Command.Options.at("--phase");
	if (Phase != "prepare" && Phase != "resume")
	{
		throw std::runtime_error("export-ue-actions requires --phase prepare or resume.");
	}
	UeHookBuild::ExportGameActions(Target, Path::PathFromUtf8(Command.Options.at("--actions")), Phase == "prepare");
	return 0;
}

int UeCommands::ExecuteUeActions(const CommandArguments& Command)
{
	auto Target = ReadTarget(Command);
	Target.Configuration = Command.Options.at("--configuration");
	UeHookBuild::ExecuteGameActions(Target, Path::PathFromUtf8(Command.Options.at("--actions")));
	return 0;
}

int UeCommands::BuildTarget(const CommandArguments& Command)
{
	auto Target = ReadTarget(Command);
	Target.Mode = Command.Options.at("--mode");
	if (Target.Mode != "normal" && Target.Mode != "rebuild")
	{
		throw std::runtime_error(
		    "build-ue-target mode must be normal or rebuild. Use prepare-metadata for Editor bootstrap.");
	}
	Target.Configuration = Command.Options.at("--configuration");
	Target.Metadata = Path::PathFromUtf8(Command.Options.at("--metadata"));
	Target.SourceCommit = Command.Options.at("--commit");
	Target.EngineCommit = Command.Options.at("--engine-commit");
	UeHookBuild::Run(Target);
	return 0;
}

int UeCommands::TestTarget(const CommandArguments& Command)
{
	const auto Target = ReadTarget(Command);
	const auto Engine = UeInvocation::EngineDirectory(Target.Engine);
	const auto Project = fs::canonical(Path::FileSystemPath(Target.Project));
	const auto Editor = UeInvocation::Editor(Engine, Target);
	const auto Receipt = UeInvocation::Receipt(Project, Target);
	if (ContextDocument::ReadJson(Receipt).at("TargetType") != "Editor")
	{
		throw std::runtime_error("Automation requires a normal Editor target.");
	}
	const auto Contexts = Project.parent_path() / "Intermediate/ModTools/ModHeaderTool/HostTargets" / Target.Target /
	                      Target.Platform / Target.Configuration / Target.Architecture / "watch.json";
	const auto Link = UeBuildAdapter::FinalizeUeTarget(Contexts, Receipt);
	if (Link.at("binaries").empty())
	{
		throw std::runtime_error("Automation requires a verified Editor host binding.");
	}
	const auto Report = fs::absolute(Path::PathFromUtf8(Command.Options.at("--report")));
	if (!fs::create_directories(Report))
	{
		throw std::runtime_error("Automation requires a new report directory.");
	}
	UeInvocation::Run(Editor,
	                  {Path::PathToUtf8(Project), "-ExecCmds=Automation RunTests StartsWith:Nelaric;SoftQuit",
	                   "-TestExit=Automation Test Queue Empty", "-ReportExportPath=" + Path::PathToUtf8(Report),
	                   "-abslog=" + Path::PathToUtf8(Report / "editor.log"), "-unattended", "-nullrhi", "-nosound",
	                   "-nosplash", "-SkipCompile", "-noP4", "-stdout", "-ddc=InstalledNoZenLocalFallback"},
	                  Project.parent_path(), ProcessEnvironment::CaptureEnvironment(), "Editor automation");
	VerifyAutomation(Report);
	return 0;
}

int UeCommands::RefreshWatches(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	UeBuildAdapter::RefreshUeWatches(Path::PathFromUtf8(Options.at("--contexts")));
	return 0;
}

int UeCommands::FinalizeTarget(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	std::cout << UeBuildAdapter::FinalizeUeTarget(Path::PathFromUtf8(Options.at("--contexts")),
	                                              Path::PathFromUtf8(Options.at("--ue-target-receipt")))
	                 .dump(2)
	          << '\n';
	return std::cout ? 0 : 1;
}
} // namespace Mod::BuildTool
