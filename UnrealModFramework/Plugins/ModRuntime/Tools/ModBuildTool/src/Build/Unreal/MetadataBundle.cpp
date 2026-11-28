// Copyright (c) 2026 Nelaric Contributors
#include "MetadataBundle.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"
#include "Common/Hash/Hash.h"
#include "Common/Process/ProcessEnvironment.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/OwnedStage.h"
#include "ModBuildTool/src/Build/Unreal/UeHookBuild.h"
#include "ModBuildTool/src/Build/Unreal/UeInvocation.h"

#include "ModExportMetadataSchema.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace Mod::BuildTool
{
namespace
{
constexpr const char* Format = "UMF.MetadataBundle";
constexpr const char* Generator = "ModBuildTool.Metadata/2";
constexpr const char* Payloads[] = {"options.json", "blueprints.json", "inputs.json"};
namespace Schema = ExportMetadata::Schema;

struct Roots
{
	fs::path Project;
	fs::path Engine;

	std::string Encode(const fs::path& Path) const
	{
		const auto File = BuildTool::Path::ResolvePath({}, BuildTool::Path::PathToUtf8(Path));
		for (const auto& [Name, Root] : {std::pair{"Project/", Project.parent_path()}, std::pair{"Engine/", Engine}})
		{
			auto Relative = File.lexically_relative(Root);
			if (!Relative.empty() && Relative != "." && *Relative.begin() != "..")
			{
				return Name + BuildTool::Path::PathToUtf8(Relative);
			}
		}
		throw std::runtime_error("Metadata input is outside the Project and Engine roots: " +
		                         BuildTool::Path::PathToUtf8(Path));
	}

	fs::path Resolve(std::string_view Logical) const
	{
		if (Logical.find_first_of("\\:") != std::string_view::npos)
		{
			throw std::runtime_error("Metadata paths must use portable, root-relative names.");
		}
		if (Logical.starts_with("Project/"))
		{
			return Path::ResolveGeneratedPath(Project.parent_path(), Logical.substr(8));
		}
		if (Logical.starts_with("Engine/"))
		{
			return Path::ResolveGeneratedPath(Engine, Logical.substr(7));
		}
		throw std::runtime_error("Unknown metadata input root: " + std::string(Logical));
	}
};

Json Identity(const UeHookTarget& Target)
{
	return {{"name", Target.Target},
	        {"platform", Target.Platform},
	        {"architecture", Target.Architecture},
	        {"configuration", Target.Configuration}};
}

void RequireCommits(const UeHookTarget& Target)
{
	for (const auto* Commit : {&Target.SourceCommit, &Target.EngineCommit})
	{
		if (Commit->size() != 40 || Commit->find_first_not_of("0123456789abcdef") != std::string::npos)
		{
			throw std::runtime_error("Metadata requires full lowercase source and engine commit IDs.");
		}
	}
}

Json EngineIdentity(const UeHookTarget& Target, const Roots& Files)
{
	const auto Version = ContextDocument::ReadJson(Files.Engine / "Build/Build.version");
	return {{"major", Version.at("MajorVersion")},
	        {"minor", Version.at("MinorVersion")},
	        {"patch", Version.at("PatchVersion")},
	        {"upstreamCommit", Target.EngineCommit}};
}

std::string Lower(std::string Value)
{
	std::ranges::transform(Value, Value.begin(),
	                       [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
	return Value;
}

bool IsUserConfig(const fs::path& File)
{
	const auto Name = Lower(Path::PathToUtf8(File.filename()));
	return Name.ends_with(".ini") && (Name.starts_with("user") || Name == "defaulteditorperprojectusersettings.ini");
}

std::string Trim(std::string Value)
{
	const auto First = Value.find_first_not_of(" \t\r");
	const auto Last = Value.find_last_not_of(" \t\r");
	return First == Value.npos ? "" : Value.substr(First, Last - First + 1);
}

std::string NormalizedText(const fs::path& FilePath)
{
	std::string Input = File::ReadFile(FilePath);
	std::string Text;
	Text.reserve(Input.size());
	for (std::size_t I = Input.starts_with("\xef\xbb\xbf") ? 3 : 0; I < Input.size(); ++I)
	{
		if (Input[I] != '\r' || I + 1 == Input.size() || Input[I + 1] != '\n')
		{
			Text.push_back(Input[I]);
		}
	}
	return Text;
}

void Walk(const fs::path& Root, const auto& Visit, bool bIncludeContent = false)
{
	if (!fs::is_directory(Path::FileSystemPath(Root)))
	{
		return;
	}
	for (auto It = fs::recursive_directory_iterator(Path::FileSystemPath(Root));
	     It != fs::recursive_directory_iterator(); ++It)
	{
		if (It->is_directory())
		{
			const auto Name = Lower(Path::PathToUtf8(It->path().filename()));
			if (Name == "binaries" || Name == "intermediate" || Name == "saved" || Name == "obj" || Name == "bin" ||
			    Name == "tools" || (!bIncludeContent && Name == "content"))
			{
				It.disable_recursion_pending();
			}
		}
		else if (It->is_regular_file())
		{
			Visit(It->path());
		}
	}
}

Json SourceInputs(const Roots& Files)
{
	Json Result = Json::object();
	const auto Record = [&](const fs::path& Input)
	{ Result[Files.Encode(Input)] = Hash::HashBytes(NormalizedText(Input)); };
	Record(Files.Project);
	auto Scope = Files.Project;
	Scope.replace_extension(".modexport");
	if (fs::is_regular_file(Path::FileSystemPath(Scope)))
	{
		Record(Scope);
	}
	for (const auto* Directory : {"Source", "Config", "Plugins", "Platforms"})
	{
		Walk(Files.Project.parent_path() / Directory,
		     [&](const fs::path& Input)
		     {
			     if (IsUserConfig(Input))
			     {
				     return;
			     }
			     const auto Extension = Lower(Path::PathToUtf8(Input.extension()));
			     if (Extension == ".ini" || Extension == ".json" || Extension == ".uplugin" || Extension == ".cs" ||
			         Extension == ".csproj" || Extension == ".h" || Extension == ".hpp" || Extension == ".inl" ||
			         Extension == ".c" || Extension == ".cpp" || Extension == ".cc" || Extension == ".mm")
			     {
				     Record(Input);
			     }
		     });
	}
	return Result;
}

Json AssetInventory(const Roots& Files)
{
	Json Result = Json::object();
	for (const auto* Directory : {"Content", "Plugins"})
	{
		Walk(
		    Files.Project.parent_path() / Directory,
		    [&](const fs::path& Asset)
		    {
			    const auto Extension = Lower(Path::PathToUtf8(Asset.extension()));
			    if (Extension == ".uasset" || Extension == ".umap")
			    {
				    Result[Files.Encode(Asset)] = Hash::HashFile(Asset);
			    }
		    },
		    true);
	}
	return Result;
}

std::string ConfigProjection(const fs::path& Input)
{
	std::istringstream Ini(NormalizedText(Input));
	std::string Section, Line, Projection;
	while (std::getline(Ini, Line))
	{
		Line = Trim(std::move(Line));
		if (Line.starts_with('[') && Line.ends_with(']'))
		{
			Section = Lower(Trim(Line.substr(1, Line.size() - 2)));
		}
		const auto Equals = Line.find('=');
		if (Equals == Line.npos || Line.starts_with(';') || Line.starts_with('#'))
		{
			continue;
		}
		const auto Key = Lower(Trim(Line.substr(0, Equals)));
		if (Section == "modexportmetadata" || Section == "coreredirects")
		{
			Projection += Section + ":" + Key + "=" + Trim(Line.substr(Equals + 1)) + "\n";
		}
	}
	return Projection;
}

std::string_view IniPlatform(std::string_view Platform)
{
	if (Platform == "Win64")
	{
		return "Windows";
	}
	if (Platform == "LinuxArm64")
	{
		return "Linux";
	}
	return Platform;
}

bool RelevantPlatform(const fs::path& Input, std::string_view Platform)
{
	const auto Family = Lower(std::string(IniPlatform(Platform)));
	const std::set<std::string_view> Platforms{"windows", "mac", "linux", "linuxarm64",
	                                           "android", "ios", "tvos",  "visionos"};
	for (const auto& Part : Input)
	{
		const auto Name = Lower(Path::PathToUtf8(Part));
		if (Platforms.contains(Name) && Name != Family && (Name != "linuxarm64" || Family != "linux"))
		{
			return false;
		}
	}
	return true;
}

Json ConfigInputs(const Roots& Files, const Json& ConfigRoots, std::string_view Platform, bool bCrossArchitecture)
{
	std::set<std::string> Search = ConfigRoots.get<std::set<std::string>>();
	const std::string Family(IniPlatform(Platform));
	for (const auto* Root : {"Project/", "Engine/"})
	{
		Search.insert(std::string(Root) + "Config");
		Search.insert(std::string(Root) + "Platforms/" + Family + "/Config");
		if (std::string_view(Family) == "Linux")
		{
			Search.insert(std::string(Root) + "Platforms/LinuxArm64/Config");
		}
	}
	Json Result = Json::object();
	for (const auto& Directory : Search)
	{
		Walk(Files.Resolve(Directory),
		     [&](const fs::path& Input)
		     {
			     if (Lower(Path::PathToUtf8(Input.extension())) != ".ini")
			     {
				     return;
			     }
			     const auto Logical = Files.Encode(Input);
			     if (!RelevantPlatform(Path::PathFromUtf8(Logical), Platform))
			     {
				     return;
			     }
			     const auto Projection = ConfigProjection(Input);
			     if (Projection.empty())
			     {
				     return;
			     }
			     const auto Key = Lower(Logical);
			     if (IsUserConfig(Input) || Key.find("/custom/") != Key.npos || Key.find("editor.ini") != Key.npos ||
			         Key.find("editor/") != Key.npos ||
			         (bCrossArchitecture && (Key.find("arm64") != Key.npos || Key.find("x64") != Key.npos)))
			     {
				     throw std::runtime_error("Target-specific configuration cannot share these metadata semantics: " +
				                              Logical);
			     }
			     Result[Logical] = Hash::HashBytes(Projection);
		     });
	}
	return Result;
}

Json DiscoverTargetTypes(const Roots& Files, const fs::path& Output)
{
	const auto Environment = ProcessEnvironment::CaptureEnvironment();
	const auto Ubt = Files.Engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll";
	UeInvocation::Run(UeInvocation::Dotnet(Files.Engine, Environment),
	                  {Path::PathToUtf8(Ubt), "-Mode=QueryTargets", "-Project=" + Path::PathToUtf8(Files.Project),
	                   "-IncludeAllTargets", "-DontIncludeParentAssembly", "-NoEngineChanges",
	                   "-Output=" + Path::PathToUtf8(Output)},
	                  Files.Project.parent_path(), Environment, "query project targets");
	const auto Document = ContextDocument::ReadJson(Output);
	Json Types = Json::object();
	for (const auto& Target : Document.at("Targets"))
	{
		const auto Name = ContextDocument::Text(Target, "Name");
		if (Name.empty() ||
		    Name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != Name.npos ||
		    Types.contains(Name))
		{
			throw std::runtime_error("UBT returned an invalid or duplicate project target: " + Name);
		}
		Types[Name] = ContextDocument::Text(Target, "Type");
	}
	return Types;
}

Json AllowedTargets(const Json& Types, const UeHookTarget& Producer)
{
	if (!Types.is_object() || Types.value(Producer.Target, "") != "Editor" || Producer.Configuration != "Development" ||
	    ((Producer.Platform == "Win64" || Producer.Platform == "Linux") && Producer.Architecture != "x64") ||
	    (Producer.Platform != "Win64" && Producer.Platform != "Linux" && Producer.Platform != "Mac") ||
	    (Producer.Architecture != "x64" && Producer.Architecture != "arm64"))
	{
		throw std::runtime_error("Metadata requires a project Development Editor as the producer.");
	}
	Json Targets = Json::array({Identity(Producer)});
	for (const auto& [Name, Type] : Types.items())
	{
		if (Type != "Game" && Type != "Client" && Type != "Server")
		{
			continue;
		}
		const auto Add = [&](std::string_view Platform, std::string_view Architecture)
		{
			auto Consumer = Producer;
			Consumer.Target = Name;
			Consumer.Platform = Platform;
			Consumer.Architecture = Architecture;
			Targets.push_back(Identity(Consumer));
		};
		Add(Producer.Platform, Producer.Architecture);
		if (Producer.Platform == "Win64")
		{
			Add("Win64", "arm64");
			Add("Win64", "arm64ec");
		}
		else if (Producer.Platform == "Linux")
		{
			Add("LinuxArm64", "arm64");
			Add("Android", "x64");
			Add("Android", "arm64");
		}
		else
		{
			for (const auto* AppleTarget : {"IOS", "TVOS", "VisionOS"})
			{
				Add(AppleTarget, "arm64");
				Add(AppleTarget, std::string_view(AppleTarget) == "TVOS" ? "tvossimulator" : "iossimulator");
			}
		}
	}
	return Targets;
}

void ValidateBlueprints(const Json& Blueprints)
{
	if (Blueprints.at("format") != "UMF.BlueprintExports" || Blueprints.at("schemaVersion") != 1 ||
	    !Blueprints.at("types").is_array() || !Blueprints.at("inputs").is_array())
	{
		throw std::runtime_error("Invalid Blueprint export snapshot.");
	}
	std::set<std::string> TypeIds;
	std::set<std::string> Classes;
	for (const auto& Type : Blueprints.at("types"))
	{
		if (!TypeIds.insert(Lower(ContextDocument::Text(Type, "typeId"))).second ||
		    !Classes.insert(ContextDocument::Text(Type, "class")).second)
		{
			throw std::runtime_error("Duplicate Blueprint export identity.");
		}
		for (const auto& Extension : Type.at("extensions"))
		{
			if (Extension.contains("structLayout") ||
			    (Extension.contains("structPath") && Extension.value("structShape", "").empty()))
			{
				throw std::runtime_error(
				    "Blueprint struct metadata must describe reflected fields without native layout.");
			}
		}
	}
}

struct Bundle
{
	std::string ManifestHash;
	Json Options;
	Json Blueprints;
	Json Inputs;
};

Bundle ReadBundle(const UeHookTarget& Target, const Roots& Files)
{
	RequireCommits(Target);
	const auto Directory = fs::canonical(Path::FileSystemPath(Target.Metadata));
	const auto Bytes = File::ReadFile(Directory / "manifest.json");
	const auto Manifest = Json::parse(Bytes);
	if (Manifest.at("format") != Format || Manifest.at("schemaVersion") != 1 ||
	    Manifest.at("generatorVersion") != Generator || Manifest.at("sourceCommit") != Target.SourceCommit ||
	    Manifest.at("engine") != EngineIdentity(Target, Files) || Manifest.at("project") != Files.Encode(Files.Project))
	{
		throw std::runtime_error("Metadata schema, project, source commit or engine revision does not match.");
	}
	const auto IdentityDocument = Manifest.at("producer");
	UeHookTarget Producer = Target;
	Producer.Target = IdentityDocument.at("name").get<std::string>();
	Producer.Platform = IdentityDocument.at("platform").get<std::string>();
	Producer.Architecture = IdentityDocument.at("architecture").get<std::string>();
	Producer.Configuration = IdentityDocument.at("configuration").get<std::string>();
	const auto Read = [&](const char* Name)
	{
		const auto Payload = File::ReadFile(Path::ResolveGeneratedPath(Directory, Name));
		if (Manifest.at("payloads").at(Name) != Hash::HashBytes(Payload))
		{
			throw std::runtime_error("Metadata payload integrity check failed: " + std::string(Name));
		}
		return Json::parse(Payload);
	};
	Bundle Result{Hash::HashBytes(Bytes), Read("options.json"), Read("blueprints.json"), Read("inputs.json")};
	ValidateBlueprints(Result.Blueprints);
	const auto Targets = AllowedTargets(Result.Inputs.at("targetTypes"), Producer);
	if (Manifest.at("consumers") != Targets || std::ranges::find(Targets, Identity(Target)) == Targets.end())
	{
		throw std::runtime_error("Metadata producer does not cover the requested target.");
	}
	if (Result.Inputs.at("assetInventory") != AssetInventory(Files))
	{
		throw std::runtime_error("Metadata is stale: the project Blueprint discovery inputs have changed.");
	}
	if (Result.Options.at("SchemaVersion") != Schema::SnapshotVersion ||
	    Result.Options.at("Project") != Manifest.at("project") || Result.Inputs.at("sources") != SourceInputs(Files))
	{
		throw std::runtime_error("Metadata is stale: project sources, configuration or descriptors have changed.");
	}
	for (const auto& [Logical, Digest] : Result.Inputs.at("assets").items())
	{
		if (Digest != Hash::HashFile(Files.Resolve(Logical)))
		{
			throw std::runtime_error("Metadata is stale: Blueprint package dependency changed: " + Logical);
		}
	}
	const auto Current = ConfigInputs(Files, Result.Inputs.at("configRoots"), Target.Platform,
	                                  Producer.Architecture != Target.Architecture);
	const auto& Expected = Result.Inputs.at("config");
	if (Current != Expected)
	{
		std::string Changed;
		for (const auto& [Path, Digest] : Current.items())
		{
			if (!Expected.contains(Path) || Expected.at(Path) != Digest)
			{
				Changed = Path;
				break;
			}
		}
		for (const auto& [Path, Digest] : Expected.items())
		{
			if (Changed.empty() && !Current.contains(Path))
			{
				Changed = Path;
			}
		}
		throw std::runtime_error("Target configuration differs from the Editor metadata inputs: " + Changed);
	}
	return Result;
}

std::string RewriteLocation(const std::string& Location, const auto& Map)
{
	const auto Suffix = Location.rfind('(');
	return Suffix == Location.npos ? Map(Location) : Map(Location.substr(0, Suffix)) + Location.substr(Suffix);
}

void WriteChanged(const fs::path& Output, const std::string& Content)
{
	if (!fs::is_regular_file(Path::FileSystemPath(Output)) || File::ReadFile(Output) != Content)
	{
		File::WriteFileAtomically(Output, Content);
	}
}

} // namespace

void MetadataBundle::Prepare(const UeHookTarget& Producer, EPhase Phase)
{
	RequireCommits(Producer);
	const Roots Files{fs::canonical(Path::FileSystemPath(Producer.Project)),
	                  UeInvocation::EngineDirectory(Producer.Engine)};
	const auto Editor = UeInvocation::Editor(Files.Engine, Producer);
	const auto Output = fs::weakly_canonical(Path::FileSystemPath(Producer.Metadata));
	fs::create_directories(Path::FileSystemPath(Output.parent_path()));
	const BuildLock Lock(Output);
	try
	{
		(void)ReadBundle(Producer, Files);
		std::cerr << "UMF metadata bundle is current.\n";
		return;
	}
	catch (const std::exception& Error)
	{
		std::cerr << "UMF metadata preparation: " << Error.what() << '\n';
	}

	const auto SourceBefore = SourceInputs(Files);
	const auto BootstrapPath = Files.Project.parent_path() / "Intermediate/ModTools/HookBuild" / Producer.Target /
	                           Producer.Platform / Producer.Configuration / Producer.Architecture /
	                           "editor-bootstrap.json";
	const auto DescribeBootstrap = [&]
	{
		const auto Receipt = UeInvocation::Receipt(Files.Project, Producer);
		if (ContextDocument::ReadJson(Receipt).at("TargetType") != "Editor")
		{
			throw std::runtime_error("Metadata bootstrap did not build an Editor target.");
		}
		return Json{{"target", Identity(Producer)},
		            {"project", Path::PathToUtf8(Files.Project)},
		            {"engine", Path::PathToUtf8(Files.Engine)},
		            {"commit", Producer.SourceCommit},
		            {"engineCommit", Producer.EngineCommit},
		            {"sources", Hash::HashBytes(SourceBefore.dump())},
		            {"receipt", Hash::HashFile(Receipt)}};
	};
	const auto BootstrapReady = [&]
	{
		try
		{
			return fs::is_regular_file(Path::FileSystemPath(BootstrapPath)) &&
			       ContextDocument::ReadJson(BootstrapPath) == DescribeBootstrap();
		}
		catch (const std::exception&)
		{
			return false;
		}
	};
	if (!BootstrapReady())
	{
		if (Phase == EPhase::Export)
		{
			throw std::runtime_error("Editor bootstrap is missing or stale. Run prepare-editor-bootstrap first.");
		}
		UeHookBuild::Run(Producer);
		WriteChanged(BootstrapPath, DescribeBootstrap().dump(2) + '\n');
	}
	if (Phase == EPhase::Bootstrap)
	{
		return;
	}

	const auto AssetsBefore = AssetInventory(Files);
	const auto Temporary = Output.parent_path() / (".mbt-stage-" + TemporaryName::UniqueSuffix());
	fs::create_directory(Path::FileSystemPath(Temporary));
	const OwnedStage Stage{Temporary, Output.parent_path()};
	const auto Types = DiscoverTargetTypes(Files, Stage.Path / "targets.json");
	const auto Targets = AllowedTargets(Types, Producer);
	const auto RawOptions = Stage.Path / "options.json";
	const auto RawBlueprints = Stage.Path / "blueprints.json";
	std::vector<std::string> Arguments{Path::PathToUtf8(Files.Project),
	                                   "-run=ModExportBlueprints",
	                                   "-Output=" + Path::PathToUtf8(RawBlueprints),
	                                   "-OptionsOutput=" + Path::PathToUtf8(RawOptions),
	                                   "-abslog=" + Path::PathToUtf8(Output.parent_path() / "metadata-export.log"),
	                                   "-unattended",
	                                   "-nullrhi",
	                                   "-nosound",
	                                   "-nosplash",
	                                   "-SkipCompile",
	                                   "-noP4",
	                                   "-stdout",
	                                   "-FullStdOutLogOutput",
	                                   "-ddc=InstalledNoZenLocalFallback"};
	if (Producer.Platform == "Mac")
	{
		// LaunchMac can display a hardware prompt before the headless commandlet starts.
		Arguments.insert(Arguments.end(), {"-skipminspeccheck", "-buildmachine"});
	}
	UeInvocation::Run(Editor, Arguments, Files.Project.parent_path(), ProcessEnvironment::CaptureEnvironment(),
	                  "metadata commandlet");

	Json Options = ContextDocument::ReadJson(RawOptions);
	Json Blueprints = ContextDocument::ReadJson(RawBlueprints);
	ValidateBlueprints(Blueprints);
	Json ConfigRoots = Json::array();
	for (const auto& Item : Options.at("ConfigRoots"))
	{
		ConfigRoots.push_back(Files.Encode(Path::PathFromUtf8(Item.get<std::string>())));
	}
	auto SourceAfter = SourceInputs(Files);
	if (SourceBefore != SourceAfter || AssetsBefore != AssetInventory(Files))
	{
		throw std::runtime_error("Project inputs changed during metadata preparation.");
	}
	Json Inputs = {
	    {"sources", std::move(SourceAfter)}, {"assets", Json::object()},
	    {"assetInventory", AssetsBefore},    {"targetTypes", Types},
	    {"configRoots", ConfigRoots},        {"config", ConfigInputs(Files, ConfigRoots, Producer.Platform, false)}};
	for (auto& Item : Blueprints.at("inputs"))
	{
		const auto Path = Path::PathFromUtf8(Item.get<std::string>());
		const auto Logical = Files.Encode(Path);
		Inputs["assets"][Logical] = Hash::HashFile(Path);
		Item = Logical;
	}
	for (auto& Type : Blueprints.at("types"))
	{
		Type["file"] = Files.Encode(Path::PathFromUtf8(ContextDocument::Text(Type, "file")));
	}
	Options["Project"] = Files.Encode(Files.Project);
	Options.erase("ConfigRoots");
	Options.erase("InputFiles");
	for (auto& Redirect : Options.at("FunctionRedirects"))
	{
		Redirect["Location"] = RewriteLocation(ContextDocument::Text(Redirect, "Location"), [&](const std::string& Path)
		                                       { return Files.Encode(BuildTool::Path::PathFromUtf8(Path)); });
	}

	Json Manifest = {{"format", Format},
	                 {"schemaVersion", 1},
	                 {"generatorVersion", Generator},
	                 {"project", Options.at("Project")},
	                 {"engine", EngineIdentity(Producer, Files)},
	                 {"sourceCommit", Producer.SourceCommit},
	                 {"producer", Identity(Producer)},
	                 {"consumers", Targets},
	                 {"payloads", Json::object()}};
	fs::create_directories(Path::FileSystemPath(Output));
	const Json Payload[]{std::move(Options), std::move(Blueprints), std::move(Inputs)};
	for (std::size_t Index = 0; Index < std::size(Payloads); ++Index)
	{
		const auto Bytes = Payload[Index].dump(2) + '\n';
		Manifest["payloads"][Payloads[Index]] = Hash::HashBytes(Bytes);
		WriteChanged(Output / Payloads[Index], Bytes);
	}
	WriteChanged(Output / "manifest.json", Manifest.dump(2) + '\n');
	(void)ReadBundle(Producer, Files);
}

void MetadataBundle::Validate(const UeHookTarget& Target)
{
	const Roots Files{fs::canonical(Path::FileSystemPath(Target.Project)),
	                  UeInvocation::EngineDirectory(Target.Engine)};
	(void)ReadBundle(Target, Files);
}

void MetadataBundle::Consume(const UeHookTarget& Target, const fs::path& Destination)
{
	const auto Started = std::chrono::steady_clock::now();
	const Roots Files{fs::canonical(Path::FileSystemPath(Target.Project)),
	                  UeInvocation::EngineDirectory(Target.Engine)};
	auto [ManifestHash, Options, Blueprints, Inputs] = ReadBundle(Target, Files);
	const auto Local = [&](const std::string& Path) { return BuildTool::Path::PathToUtf8(Files.Resolve(Path)); };
	Options["Project"] = Path::PathToUtf8(Files.Project);
	Options["ConfigRoots"] = Json::array();
	Options["InputFiles"] = Json::array();
	Options["Inputs"] = Json::object();
	for (const auto& Key : {"sources", "config"})
	{
		for (const auto& [Logical, Digest] : Inputs.at(Key).items())
		{
			const auto Input = Files.Resolve(Logical);
			const auto Absolute = Path::PathToUtf8(Input);
			if (!Options["Inputs"].contains(Absolute))
			{
				std::ostringstream Hex;
				Hex << std::hex << std::setfill('0') << std::setw(16) << Schema::Fingerprint(File::ReadFile(Input));
				Options["Inputs"][Absolute] = Hex.str();
				Options["InputFiles"].push_back(Absolute);
			}
		}
	}
	for (auto& Redirect : Options.at("FunctionRedirects"))
	{
		Redirect["Location"] = RewriteLocation(ContextDocument::Text(Redirect, "Location"), Local);
	}
	for (auto& Input : Blueprints.at("inputs"))
	{
		Input = Local(Input.get<std::string>());
	}
	for (auto& Type : Blueprints.at("types"))
	{
		Type["file"] = Local(ContextDocument::Text(Type, "file"));
	}
	fs::create_directories(Path::FileSystemPath(Destination));
	WriteChanged(Destination / "MetadataOptions.json", Options.dump(2) + '\n');
	WriteChanged(Destination / "BlueprintExports.json", Blueprints.dump(2) + '\n');
	WriteChanged(Destination / "bundle.sha256", ManifestHash + '\n');
	const auto Seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - Started).count();
	std::cerr << "UMF timing metadata validation " << Seconds << " s\n";
}
} // namespace Mod::BuildTool
