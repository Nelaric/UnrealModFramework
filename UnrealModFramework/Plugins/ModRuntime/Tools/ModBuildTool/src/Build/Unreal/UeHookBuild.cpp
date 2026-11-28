// Copyright (c) 2026 Nelaric Contributors
#include "UeHookBuild.h"

#include "Common/Files/ExportModules.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/Executable.h"
#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"
#include "Common/Hash/Hash.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/OwnedStage.h"
#include "ModBuildTool/src/Build/Unreal/MetadataBundle.h"
#include "ModBuildTool/src/Build/Unreal/UeBuildAdapter.h"
#include "ModBuildTool/src/Build/Unreal/UeHookActions.h"
#include "ModBuildTool/src/Build/Unreal/UeInvocation.h"

#include <filesystem>
#include <initializer_list>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::BuildTool
{
namespace
{
constexpr std::string_view UbtEntry = "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll";

struct HookSnapshot
{
	std::string MapHash;
	UeHookSources Sources;
	bool operator==(const HookSnapshot&) const = default;
};

HookSnapshot ReadHookSources(const fs::path& Generated, const std::string& Game, const UeHookTarget& Target)
{
	const Json Manifest =
	    ContextDocument::ReadJson(Path::ResolveGeneratedPath(Generated, Game + ".generated-artifacts.json"));
	const std::string Format = ContextDocument::Text(Manifest, "format");
	if ((Format != "UMF.GeneratedArtifacts" && Format != "UMF.UbtHostArtifacts") ||
	    Manifest.at("status") != "Complete" || Manifest.at("target") != Target.Target ||
	    Manifest.at("platform") != Target.Platform || Manifest.at("configuration") != Target.Configuration ||
	    Manifest.at("architecture") != Target.Architecture)
	{
		throw std::runtime_error("The MHT generation does not match this UBT target.");
	}
	std::map<std::string, std::string> Files;
	for (const auto& Entry : Manifest.at("generatedFiles"))
	{
		if (!Files.emplace(ContextDocument::Text(Entry, "path"), ContextDocument::Text(Entry, "contentHash")).second)
		{
			throw std::runtime_error("Duplicate file in the MHT generation manifest.");
		}
	}
	const auto Map = Files.find("Diagnostics/HookSources.json");
	if (Map == Files.end())
	{
		return {};
	}
	const fs::path MapPath = Path::ResolveGeneratedPath(Generated, Map->first);
	const auto Bytes = File::ReadFile(MapPath);
	if (Hash::HashBytes(Bytes) != Map->second)
	{
		throw std::runtime_error("The MHT Hook source map changed after generation.");
	}
	const auto SourceMap = Json::parse(Bytes);
	ContextDocument::RequireFormat(SourceMap, "UMF.HookSourceMap");
	const fs::path CopiesRoot = Path::ResolveGeneratedPath(Generated, "Diagnostics/HookSources");
	HookSnapshot Snapshot{Map->second, {}};
	for (const auto& Entry : SourceMap.at("units"))
	{
		const fs::path Source =
		    fs::canonical(Path::FileSystemPath(Path::PathFromUtf8(ContextDocument::Text(Entry, "source"))));
		const std::string Name = ContextDocument::Text(Entry, "generated");
		const fs::path Copy = Path::ResolveGeneratedPath(Generated, Name);
		const fs::path Relative = Copy.lexically_relative(CopiesRoot);
		const std::string GeneratedHash = ContextDocument::Text(Entry, "generatedHash");
		const auto Listed = Files.find(Name);
		if (Relative.empty() || Relative == "." || *Relative.begin() == ".." || Listed == Files.end() ||
		    Listed->second != GeneratedHash || Hash::HashFile(Copy) != GeneratedHash ||
		    Hash::HashFile(Source) != ContextDocument::Text(Entry, "sourceHash") ||
		    !Snapshot.Sources.emplace(Source, UeHookSource{Copy, GeneratedHash}).second)
		{
			throw std::runtime_error("Invalid or stale MHT Hook source substitution for " + Path::PathToUtf8(Source) +
			                         ".");
		}
	}
	return Snapshot;
}

void ReuseObjects(UeHookObjects& Objects, const UeHookSources& Sources, const fs::path& Receipt)
{
	if (Objects.size() == Sources.size())
	{
		return;
	}
	const Json Previous =
	    fs::is_regular_file(Path::FileSystemPath(Receipt)) ? ContextDocument::ReadJson(Receipt) : Json::object();
	std::map<fs::path, const Json*> BySource;
	for (const auto& [Key, Entry] : Previous.items())
	{
		BySource.emplace(Path::ResolvePath(Receipt.parent_path(), Key), &Entry);
	}
	for (const auto& [Source, Unit] : Sources)
	{
		if (Objects.contains(Source))
		{
			continue;
		}
		const auto Cached = BySource.find(Source);
		if (Cached != BySource.end())
		{
			const auto& Entry = *Cached->second;
			const auto Object = Path::PathFromUtf8(Entry.value("object", ""));
			if (Entry.value("generatedHash", "") == Unit.GeneratedHash &&
			    fs::is_regular_file(Path::FileSystemPath(Object)) &&
			    Hash::HashFile(Object) == Entry.value("objectHash", ""))
			{
				Objects.emplace(Source, Object);
				continue;
			}
		}
		throw std::runtime_error("UBT omitted the required Hook compile for " + Path::PathToUtf8(Source) +
		                         ". Use build-ue-target with --mode rebuild.");
	}
}
fs::path TargetVariant(const UeHookTarget& Target)
{
	for (const auto* Identifier : {&Target.Target, &Target.Platform, &Target.Configuration, &Target.Architecture})
	{
		if (Identifier->empty() ||
		    Identifier->find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
		        std::string::npos)
		{
			throw std::runtime_error("Supply valid UBT target, platform, configuration, and architecture identifiers.");
		}
	}
	return fs::path(Target.Target) / Target.Platform / Target.Configuration / Target.Architecture;
}

struct UbtTarget
{
	explicit UbtTarget(const UeHookTarget& Target)
	    : Variant(TargetVariant(Target)), Engine(UeInvocation::EngineDirectory(Target.Engine)),
	      Project(fs::canonical(Path::FileSystemPath(Target.Project))),
	      ModTools(Project.parent_path() / "Intermediate/ModTools"),
	      HostTarget(ModTools / "ModHeaderTool/HostTargets" / Variant), Work(ModTools / "HookBuild" / Variant),
	      Environment(ProcessEnvironment::CaptureEnvironment()), Dotnet(UeInvocation::Dotnet(Engine, Environment))
	{
		if (!fs::is_regular_file(Path::FileSystemPath(Project)) || Project.extension() != ".uproject")
		{
			throw std::runtime_error("Supply an Unreal project .uproject file.");
		}
		fs::create_directories(Path::FileSystemPath(Work));
		Environment["UMF_MBT_EXECUTABLE"] = Path::PathToUtf8(Executable::ExecutablePath());
		Environment["UMF_NATIVE_TOOLS_READY"] = "1";
		Environment.erase("UMF_METADATA_INPUTS");
#ifdef _WIN32
		Environment["UMF_RUNNING_MBT_EXECUTABLE"] = Environment.at("UMF_MBT_EXECUTABLE");
#endif
		Arguments = {Target.Target,
		             Target.Platform,
		             Target.Configuration,
		             "-Architecture=" + Target.Architecture,
		             "-Project=" + Path::PathToUtf8(Project),
		             "-WaitMutex",
		             "-NoEngineChanges",
		             "-MaxParallelActions=4"};
		if (Target.Platform == "Linux" || Target.Platform == "LinuxArm64")
		{
			Arguments.emplace_back("-NoDumpSyms");
		}
		if (Target.Platform == "Win64")
		{
			for (const auto& [Name, Flag] : {std::pair{"UMF_MSVC_VERSION", "-CompilerVersion="},
			                                 std::pair{"UMF_WINDOWS_SDK_VERSION", "-WindowsSDKVersion="}})
			{
				if (const auto Value = Environment.find(Name); Value != Environment.end() && !Value->second.empty())
				{
					Arguments.emplace_back(std::string(Flag) + Value->second);
				}
			}
		}
	}

	std::vector<std::string> ExportArguments(Json& Modules) const
	{
		std::vector<std::string> Export{Path::PathToUtf8(Engine / UbtEntry)};
		Export.insert(Export.end(), Arguments.begin(), Arguments.end());
		std::string Names;
		for (const auto& [Name, Directory] : Tools::ExportModules::Discover(Project).Modules)
		{
			Names += (Names.empty() ? "" : "+") + Name;
			Modules[Name] = Path::PathToUtf8(Directory);
		}
		Export.push_back("-ModExportModules=" + Names);
		Export.push_back("-Log=" + Path::PathToUtf8(Work / "UbtBuild.log"));
		Export.emplace_back("-NoHotReloadFromIDE");
		return Export;
	}

	void Run(const std::vector<std::string>& Command, std::string_view Step) const
	{
		UeInvocation::Run(Dotnet, Command, Engine / "Source", Environment, Step);
	}

	void Execute(const fs::path& Actions)
	{
#ifdef _WIN32
		Environment["ISBUILDMACHINE"] = "1";
#else
		Environment["IsBuildMachine"] = "1";
#endif
		std::vector<std::string> Execute{Path::PathToUtf8(Engine / UbtEntry), "-Mode=Execute",
		                                 "-Actions=" + Path::PathToUtf8(Actions),
		                                 "-Log=" + Path::PathToUtf8(Work / "UbtExecute.log"), "-NoUBA"};
		Execute.insert(Execute.end(), Arguments.begin(), Arguments.end());
		Run(Execute, "action execution");
	}

	fs::path Variant;
	fs::path Engine;
	fs::path Project;
	fs::path ModTools;
	fs::path HostTarget;
	fs::path Work;
	std::map<std::string, std::string> Environment;
	fs::path Dotnet;
	std::vector<std::string> Arguments;
};

void AssertMonolithicGame(const UbtTarget& Ubt, const UeHookTarget& Target)
{
	const auto Rules = ContextDocument::ReadJson(Ubt.HostTarget / "Target.json").at("target");
	if (Target.Configuration != "Development" || Rules.at("type") != "Game" || Rules.at("linkType") != "Monolithic" ||
	    Rules.at("name") != Target.Target || Rules.at("platform") != Target.Platform ||
	    Rules.at("architecture") != Target.Architecture || Rules.at("configuration") != Target.Configuration)
	{
		throw std::runtime_error("Split builds require the actual monolithic Development Game target.");
	}
}
} // namespace

void UeHookBuild::Run(const UeHookTarget& Target)
{
	if (Target.Mode != "normal" && Target.Mode != "rebuild" && Target.Mode != "bootstrap")
	{
		throw std::runtime_error("build-ue-target mode must be normal, rebuild, or bootstrap.");
	}
	UbtTarget Ubt(Target);
	const BuildLock Lock(Ubt.Work / ".build", ".lock");
	Json Modules = Json::object();
	auto Export = Ubt.ExportArguments(Modules);
	if (Target.Mode == "bootstrap")
	{
		Export.emplace_back("-MhtBootstrap");
		Export.emplace_back("-NoUBA");
		Ubt.Run(Export, "bootstrap build");
		return;
	}

	const auto Inputs = Ubt.HostTarget / "Metadata";
	MetadataBundle::Consume(Target, Inputs);
	Ubt.Environment["UMF_METADATA_INPUTS"] = Path::PathToUtf8(Inputs);

	const auto Game = Path::PathToUtf8(Ubt.Project.stem());
	const auto StagePath = Ubt.Work / (".mbt-stage-" + TemporaryName::UniqueSuffix());
	if (!fs::create_directory(Path::FileSystemPath(StagePath)))
	{
		throw std::runtime_error("Cannot create a fresh UBT action stage.");
	}
	const OwnedStage Stage{StagePath, Ubt.Work};
	const auto ActionsFile = Stage.Path / "Actions.json";
	const auto Receipt = Ubt.Work / "objects.json";
	Export.push_back("-WriteOutdatedActions=" + Path::PathToUtf8(ActionsFile));
	if (Target.Mode == "rebuild")
	{
		Export.emplace_back("-Rebuild");
	}
	Ubt.Run(Export, "action export");
	const auto Generated = fs::canonical(Path::FileSystemPath(Ubt.ModTools / "ModHeaderTool/Generated" / Ubt.Variant));
	const auto Sources = ReadHookSources(Generated, Game, Target);
	auto Graph = ContextDocument::ReadJson(ActionsFile);
	auto Objects = UeHookActions::Prepare(Graph, Sources.Sources, Stage.Path);
	ReuseObjects(Objects, Sources.Sources, Receipt);
	if (!Graph.at("Actions").empty())
	{
		File::WriteFileAtomically(ActionsFile, Graph.dump(2));
		Ubt.Execute(ActionsFile);
	}
	if (ReadHookSources(Generated, Game, Target) != Sources)
	{
		throw std::runtime_error("The MHT Hook source map changed while the build was running.");
	}
	if ((Target.Platform == "Win64" || Target.Platform == "Linux" || Target.Platform == "LinuxArm64" ||
	     Target.Platform == "Mac") &&
	    (Target.Architecture == "x64" || Target.Architecture == "arm64"))
	{
		const auto Contexts = Ubt.HostTarget / "watch.json";
		const auto Link = UeBuildAdapter::FinalizeUeTarget(Contexts, UeInvocation::Receipt(Ubt.Project, Target));
		if (!Link.is_object() || Link.at("binaries").empty())
		{
			throw std::runtime_error("The UBT build did not verify a linked UMF host binding.");
		}
	}

	Json Audit = Json::object();
	for (const auto& [Source, Object] : Objects)
	{
		Audit[Path::PathToUtf8(Source)] = {{"generatedHash", Sources.Sources.at(Source).GeneratedHash},
		                                   {"object", Path::PathToUtf8(Object)},
		                                   {"objectHash", Hash::HashFile(Object)}};
	}
	File::WriteFileAtomically(Receipt, Audit.dump(2) + '\n');
}

fs::path UeHookBuild::MetadataInputs(const UeHookTarget& Target)
{
	return fs::canonical(Path::FileSystemPath(Target.Project)).parent_path() /
	       "Intermediate/ModTools/ModHeaderTool/HostTargets" / TargetVariant(Target) / "Metadata";
}

void UeHookBuild::ExportGameActions(const UeHookTarget& Target, const fs::path& Actions, bool bPrepare)
{
	UbtTarget Ubt(Target);
	const BuildLock Lock(Ubt.Work / ".build", ".lock");
	Json Modules = Json::object();
	auto Command = Ubt.ExportArguments(Modules);
	if (Target.Platform == "Win64")
	{
		// A regenerated MSVC PCH must provide the same CodeView types to objects compiled on the producer.
		Command.emplace_back("-Deterministic");
	}
	if (bPrepare)
	{
		Command.emplace_back("-UmfPrepareGame");
	}
	else
	{
		const auto Inputs = Ubt.HostTarget / "Metadata";
		if (!fs::is_regular_file(Path::FileSystemPath(Inputs / "bundle.sha256")))
		{
			throw std::runtime_error("Import validated Metadata before exporting the resumed action graph.");
		}
		Ubt.Environment["UMF_METADATA_INPUTS"] = Path::PathToUtf8(Inputs);
	}
	const auto Output = fs::absolute(Path::FileSystemPath(Actions));
	fs::create_directories(Output.parent_path());
	Command.push_back("-WriteOutdatedActions=" + Path::PathToUtf8(Output));
	Ubt.Run(Command, bPrepare ? "Game preparation action export" : "Game resume action export");
	AssertMonolithicGame(Ubt, Target);

	auto Graph = ContextDocument::ReadJson(Output);
	if (!bPrepare)
	{
		const auto Directory =
		    fs::canonical(Path::FileSystemPath(Ubt.ModTools / "ModHeaderTool/Generated" / Ubt.Variant));
		auto Sources = ReadHookSources(Directory, Path::PathToUtf8(Ubt.Project.stem()), Target);
		(void)UeHookActions::Prepare(Graph, Sources.Sources, Output.parent_path());
	}
	else
	{
		// Recover the same dependency edges that UBT's exported executor graph uses after a cold export.
		(void)UeHookActions::Prepare(Graph, {}, Output.parent_path());
	}
	std::vector<std::string> Receipts;
	for (const auto& Action : Graph.at("Actions"))
	{
		if (Action.at("Type") != "WriteMetadata")
		{
			continue;
		}
		for (const auto& Item : Action.at("ProducedItems"))
		{
			const auto Path = Item.get<std::string>();
			if (Path.ends_with(".target"))
			{
				Receipts.push_back(Path);
			}
		}
	}
	if (Receipts.size() != 1)
	{
		throw std::runtime_error("Game action graph must produce exactly one UBT target receipt.");
	}
	Graph["umf"] = {{"engine", Path::PathToUtf8(Ubt.Engine)},
	                {"modules", std::move(Modules)},
	                {"targetReceipt", Receipts.front()},
	                {"contexts", Path::PathToUtf8(Ubt.HostTarget / "watch.json")}};
	File::WriteFileAtomically(Output, Graph.dump(2) + '\n');
}

void UeHookBuild::ExecuteGameActions(const UeHookTarget& Target, const fs::path& Actions)
{
	UbtTarget Ubt(Target);
	const BuildLock Lock(Ubt.Work / ".build", ".lock");
	AssertMonolithicGame(Ubt, Target);
	const auto Graph = fs::canonical(Path::FileSystemPath(Actions));
	if (!ContextDocument::ReadJson(Graph).at("Actions").empty())
	{
		Ubt.Execute(Graph);
	}
}
} // namespace Mod::BuildTool
