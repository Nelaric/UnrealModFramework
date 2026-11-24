// Copyright (c) 2026 Nelaric Contributors
#include "UeHookBuild.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/Executable.h"
#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"
#include "Common/Hash/Hash.h"
#include "Common/Process/Process.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/OwnedStage.h"
#include "ModBuildTool/src/Build/Unreal/UeHookActions.h"

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

fs::path FindDotnet(const fs::path& Engine)
{
#if defined(_WIN32)
	constexpr std::string_view Host = "win";
	constexpr std::string_view Executable = "dotnet.exe";
#elif defined(__APPLE__)
	constexpr std::string_view Host = "mac";
	constexpr std::string_view Executable = "dotnet";
#else
	constexpr std::string_view Host = "linux";
	constexpr std::string_view Executable = "dotnet";
#endif
#if defined(_M_ARM64) || defined(__aarch64__)
	constexpr std::string_view Architecture = "arm64";
#elif defined(_M_X64) || defined(__x86_64__)
	constexpr std::string_view Architecture = "x64";
#else
	throw std::runtime_error("Unsupported UBT build host architecture.");
	constexpr std::string_view Architecture = "";
#endif
	const fs::path Runtimes = Engine / "Binaries/ThirdParty/DotNet";
	const auto Find = [&](std::string_view Arch)
	{
		const std::string Runtime = std::string(Host) + "-" + std::string(Arch);
		fs::path Dotnet;
		for (const auto& Entry : fs::directory_iterator(Runtimes))
		{
			const fs::path Candidate = Entry.path() / Runtime / Executable;
			if (Candidate > Dotnet && fs::is_regular_file(Candidate))
			{
				Dotnet = Candidate;
			}
		}
		return Dotnet;
	};
	fs::path Dotnet = Find(Architecture);
	if (Dotnet.empty() && Host == "win" && Architecture == "arm64")
	{
		Dotnet = Find("x64");
	}
	if (Dotnet.empty())
	{
		throw std::runtime_error("Cannot find the engine's bundled .NET for " + std::string(Host) + "-" +
		                         std::string(Architecture) + " under " + Path::PathToUtf8(Engine) + ".");
	}
	return Dotnet;
}

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
		const fs::path Source = fs::canonical(Path::PathFromUtf8(ContextDocument::Text(Entry, "source")));
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
	const Json Previous = fs::is_regular_file(Receipt) ? ContextDocument::ReadJson(Receipt) : Json::object();
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
			if (Entry.value("generatedHash", "") == Unit.GeneratedHash && fs::is_regular_file(Object) &&
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
} // namespace

void UeHookBuild::Run(const UeHookTarget& Target)
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
	if (Target.Mode != "normal" && Target.Mode != "rebuild" && Target.Mode != "bootstrap")
	{
		throw std::runtime_error("build-ue-target mode must be normal, rebuild, or bootstrap.");
	}
	fs::path Engine = fs::canonical(Target.Engine);
	if (!fs::is_regular_file(Engine / UbtEntry))
	{
		Engine /= "Engine";
	}
	const fs::path Ubt = Engine / UbtEntry;
	const fs::path Project = fs::canonical(Target.Project);
	if (!fs::is_regular_file(Ubt) || !fs::is_regular_file(Project) || Project.extension() != ".uproject")
	{
		throw std::runtime_error("Supply an Unreal Engine with UnrealBuildTool and a project .uproject file.");
	}
	const auto Dotnet = FindDotnet(Engine);
	auto Environment = ProcessEnvironment::CaptureEnvironment();
#ifdef _WIN32
	Environment["UMF_RUNNING_MBT_EXECUTABLE"] = Path::PathToUtf8(Executable::ExecutablePath());
#endif
	const auto RunUbt = [&](const std::vector<std::string>& Arguments, std::string_view Step)
	{
		const int Exit = Process::RunProcess(Dotnet, Arguments, Engine / "Source", Environment);
		if (Exit != 0)
		{
			throw std::runtime_error("UBT " + std::string(Step) + " failed with exit code " + std::to_string(Exit) +
			                         ".");
		}
	};
	const std::vector<std::string> TargetArguments{Target.Target,
	                                               Target.Platform,
	                                               Target.Configuration,
	                                               "-Architecture=" + Target.Architecture,
	                                               "-Project=" + Path::PathToUtf8(Project),
	                                               "-WaitMutex",
	                                               "-MaxParallelActions=6"};
	const auto Root = Project.parent_path();
	const auto ModTools = Root / "Intermediate/ModTools";
	const auto Variant = fs::path(Target.Target) / Target.Platform / Target.Configuration / Target.Architecture;
	const auto WorkPath = ModTools / "HookBuild" / Variant;
	fs::create_directories(WorkPath);
	const auto Work = fs::canonical(WorkPath);
	const BuildLock Lock(Work / ".build", ".lock");
	std::vector<std::string> Export{Path::PathToUtf8(Ubt)};
	Export.insert(Export.end(), TargetArguments.begin(), TargetArguments.end());
	Export.emplace_back("-NoHotReloadFromIDE");
	if (Target.Mode == "bootstrap")
	{
		Export.emplace_back("-MhtBootstrap");
		RunUbt(Export, "bootstrap build");
		return;
	}

	const auto Game =
	    ContextDocument::Text(ContextDocument::ReadJson(Root / "Config/ModHeaderTool.json").at("generation"), "gameId");
	const auto StagePath = Work / (".mbt-stage-" + TemporaryName::UniqueSuffix());
	if (!fs::create_directory(StagePath))
	{
		throw std::runtime_error("Cannot create a fresh UBT action stage.");
	}
	const OwnedStage Stage{StagePath, Work};
	const auto ActionsFile = Stage.Path / "Actions.json";
	const auto Receipt = Work / "objects.json";
	Export.push_back("-WriteOutdatedActions=" + Path::PathToUtf8(ActionsFile));
	if (Target.Mode == "rebuild")
	{
		Export.emplace_back("-Rebuild");
	}
	RunUbt(Export, "action export");
	const auto Generated = fs::canonical(ModTools / "ModHeaderTool/Generated" / Variant);
	const auto Sources = ReadHookSources(Generated, Game, Target);
	auto Graph = ContextDocument::ReadJson(ActionsFile);
	auto Objects = UeHookActions::Prepare(Graph, Sources.Sources, Stage.Path);
	ReuseObjects(Objects, Sources.Sources, Receipt);
	if (!Graph.at("Actions").empty())
	{
		File::WriteFileAtomically(ActionsFile, Graph.dump(2));
		std::vector<std::string> Execute{Path::PathToUtf8(Ubt), "-Mode=Execute",
		                                 "-Actions=" + Path::PathToUtf8(ActionsFile), "-NoUBA"};
		Execute.insert(Execute.end(), TargetArguments.begin(), TargetArguments.end());
		RunUbt(Execute, "action execution");
	}
	if (ReadHookSources(Generated, Game, Target) != Sources)
	{
		throw std::runtime_error("The MHT Hook source map changed while the build was running.");
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
} // namespace Mod::BuildTool
