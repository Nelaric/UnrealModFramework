// Copyright (c) 2026 Nelaric Contributors
#include "UeBuildAdapter.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Binary/BinaryInspector.h"
#include "ModBuildTool/src/Build/BuildInputs.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/HostPlan.h"
#include "ModBuildTool/src/Build/LinkReceiptVerifier.h"
#include "ModBuildTool/src/Build/Request.h"
#include "ModBuildTool/src/Build/Unreal/UeTargetReceipt.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace
{
Json FinalizeDirectHost(const Json& Batch, const Json& Target, const fs::path& Project, const fs::path& Engine,
                        const fs::path& Receipt, const std::string& ReceiptHash, const fs::path& Output)
{
	if (Batch.at("status") != "Complete" || Batch.at("unresolvedTypeCount") != 0 ||
	    Batch.at("target") != Target.at("TargetName") || Batch.at("platform") != Target.at("Platform") ||
	    Batch.at("configuration") != Target.at("Configuration") ||
	    Batch.at("architecture") != Target.at("Architecture"))
	{
		throw std::runtime_error("The UBT receipt does not match the completed generation.");
	}
	const std::string Generation = ContextDocument::Text(Batch, "generationId");
	Hash::RequireDigest(Generation, "generationId");
	const auto ProductPath = [&](const std::string& Value) -> fs::path
	{
		constexpr std::string_view Prefix = "$(EngineDir)/";
		if (Value.starts_with(Prefix) && !Engine.empty())
		{
			return Path::ResolvePath(Engine, std::string_view(Value).substr(Prefix.size()));
		}
		return UeTargetReceipt::UeProductPath(Project.parent_path(), Value);
	};
	std::set<std::string> Owners;
	for (const auto& Unit : Batch.at("compilationUnits"))
	{
		if (Unit.at("role") == "HostBinding")
		{
			Owners.insert(ContextDocument::Text(Unit, "ownerModule"));
		}
	}
	std::map<fs::path, std::string> Products;
	std::map<std::string, fs::path> Modules;
	const auto BuildId = Target.at("Version").value("BuildId", "");
	for (const auto& Item : Target.at("BuildProducts"))
	{
		const auto File = ProductPath(ContextDocument::Text(Item, "Path"));
		if (File.empty())
		{
			continue;
		}
		const auto Kind = ContextDocument::Text(Item, "Type");
		Products.emplace(File, Kind);
		if (Kind == "RequiredResource" && File.extension() == ".modules")
		{
			const auto Map = ContextDocument::ReadJson(File);
			for (const auto& [Name, Image] : Map.at("Modules").items())
			{
				if (Owners.contains(Name))
				{
					const auto Path = Path::ResolvePath(File.parent_path(), Image.get<std::string>());
					if ((!BuildId.empty() && Map.at("BuildId") != BuildId) || !Modules.emplace(Name, Path).second)
					{
						throw std::runtime_error("Ambiguous or stale UBT module manifest for " + Name);
					}
				}
			}
		}
	}
	Json Binaries = Json::array();
	for (const auto& Owner : Owners)
	{
		const bool Modular = Modules.contains(Owner);
		const fs::path Image = Modular ? Modules.at(Owner) : ProductPath(ContextDocument::Text(Target, "Launch"));
		const std::string Kind = Modular ? "DynamicLibrary" : "Executable";
		if (!Products.contains(Image) || Products.at(Image) != Kind)
		{
			throw std::runtime_error("The UBT products do not identify the owning image for " + Owner);
		}
		const std::string Witness = "UMF_HostBinding_" + Owner + "_" + Generation;
		BinaryInspector::ValidateHostBinary(Image, ContextDocument::Text(Batch, "platform"),
		                                    ContextDocument::Text(Batch, "architecture"), Kind, Witness);
		Binaries.push_back({{"ownerModule", Owner},
		                    {"path", Path::PathToUtf8(Image)},
		                    {"kind", Kind},
		                    {"contentHash", Hash::HashFile(Image)},
		                    {"linkWitness", Witness}});
	}
	if (Hash::HashFile(Receipt) != ReceiptHash)
	{
		throw std::runtime_error("The UBT target receipt changed during host verification.");
	}
	Json Record = {{"format", "UMF.UbtHostLinkReceipt"},
	               {"schemaVersion", 1},
	               {"generationId", Generation},
	               {"ueTargetReceipt", {{"path", Path::PathToUtf8(Receipt)}, {"contentHash", ReceiptHash}}},
	               {"binaries", Binaries}};
	for (const auto Field :
	     {"target", "platform", "architecture", "configuration", "contractHash", "projectionHash", "nativeABIHash"})
	{
		Record[Field] = Batch.at(Field);
	}
	File::WriteFileAtomically(Output, Record.dump(2) + '\n');
	return Record;
}
} // namespace

void UeBuildAdapter::RefreshUeWatches(const fs::path& ContextSetPath)
{
	if (!fs::is_regular_file(ContextSetPath))
	{
		return;
	}
	const fs::path ContextSet = fs::canonical(Path::FileSystemPath(ContextSetPath));
	const Json Pipeline = Json::parse(File::ReadFile(ContextSet), nullptr, false);
	if (!Pipeline.is_object())
	{
		return;
	}
	const fs::path Manifest =
	    Pipeline.contains("generatedManifest")
	        ? Path::ResolvePath(ContextSet.parent_path(), ContextDocument::Text(Pipeline, "generatedManifest"))
	        : ContextSet;
	if (!fs::is_regular_file(Manifest))
	{
		return;
	}
	const Json Batch = Manifest == ContextSet ? Pipeline : Json::parse(File::ReadFile(Manifest), nullptr, false);
	if (!Batch.is_object() || Batch.value("status", "") != "Complete" || !Batch.contains("watchedDirectories") ||
	    !Batch.contains("inputFiles"))
	{
		return;
	}
	fs::path Changed = BuildInputs::ChangedWatchedDirectory(Batch.at("watchedDirectories"));
	if (Changed.empty())
	{
		// An edit can introduce the first export in a previously ordinary module. Regather UBT's
		// actions in this build so the new UHT compilation unit reaches the owning binary.
		std::set<std::string> Sources;
		for (const auto& Watch : Batch.at("watchedDirectories"))
		{
			if (Watch.at("kind") == "Source")
			{
				for (const auto& File : Watch.at("files"))
				{
					Sources.insert(File.get<std::string>());
				}
			}
		}
		const auto GeneratedTime = fs::last_write_time(Manifest);
		for (const auto& File : Batch.at("inputFiles"))
		{
			const auto Name = ContextDocument::Text(File, "path");
			const auto Input = Path::PathFromUtf8(Name);
			const auto& Hash = File.at("contentHash").get_ref<const std::string&>();
			if (fs::is_regular_file(Input) != !Hash.empty() ||
			    (Sources.contains(Name) && !Hash.empty() && fs::last_write_time(Input) > GeneratedTime &&
			     Mod::BuildTool::Hash::HashFile(Input) != Hash))
			{
				Changed = Input;
				break;
			}
		}
	}
	if (!Changed.empty())
	{
		File::WriteFileAtomically(ContextSet.parent_path() / ".mht-watch.stamp", TemporaryName::UniqueSuffix() + '\n');
		std::cerr << "ModBuildTool: MHT watch changed: " << Path::PathToUtf8(Changed) << '\n';
	}
}

Json UeBuildAdapter::FinalizeUeTarget(const fs::path& ContextSetPath, const fs::path& TargetReceiptPath)
{
	const fs::path ContextSet = fs::canonical(Path::FileSystemPath(ContextSetPath));
	const Json Pipeline = ContextDocument::ReadJson(ContextSet);
	ContextDocument::RequireFormat(Pipeline, "UMF.ModHostContextSet");
	const fs::path Manifest =
	    Path::ResolvePath(ContextSet.parent_path(), ContextDocument::Text(Pipeline, "generatedManifest"));
	const fs::path Project = Path::ResolvePath(ContextSet.parent_path(), ContextDocument::Text(Pipeline, "project"));
	const fs::path UbtReceipt = fs::canonical(Path::FileSystemPath(TargetReceiptPath));
	const std::string UbtBytes = File::ReadFile(UbtReceipt);
	const Json Target = Json::parse(UbtBytes);
	if (Path::ResolvePath(UbtReceipt.parent_path(), ContextDocument::Text(Target, "Project")) != Project)
	{
		throw std::runtime_error("UBT target receipt belongs to a different project.");
	}
	const BuildLock Generation(Manifest.parent_path() / ".mht.lock", "");
	const auto Batch = ContextDocument::ReadJson(Manifest);
	if (Batch.at("format") == "UMF.GeneratedArtifacts" && Batch.at("schemaVersion") == 4)
	{
		const auto Engine =
		    Pipeline.contains("engineDirectory")
		        ? Path::ResolvePath(ContextSet.parent_path(), ContextDocument::Text(Pipeline, "engineDirectory"))
		        : fs::path{};
		return FinalizeDirectHost(Batch, Target, Project, Engine, UbtReceipt, Hash::HashBytes(UbtBytes),
		                          ContextSet.parent_path() / "host-link.json");
	}
	const auto& ContextHashes = Batch.at("hostBuildContexts");
	const auto& Contexts = Pipeline.at("contexts");
	if (!Contexts.is_object() || !ContextHashes.is_object() || Contexts.size() != ContextHashes.size())
	{
		throw std::runtime_error("UE finalization requires every generated producer context.");
	}
	Json Results = Json::array();
	if (Contexts.empty())
	{
		const auto& Units = Batch.at("compilationUnits");
		const auto Client = ContextDocument::Text(Batch, "gameId") + "ModClient";
		if (Batch.at("format") != "UMF.UbtHostArtifacts" || Batch.at("status") != "Complete" ||
		    Batch.at("target") != Target.at("TargetName") || Batch.at("platform") != Target.at("Platform") ||
		    Batch.at("configuration") != Target.at("Configuration") ||
		    Batch.at("architecture") != Target.at("Architecture") || !Units.is_array() ||
		    !std::all_of(Units.begin(), Units.end(),
		                 [&](const Json& Unit) { return ContextDocument::Text(Unit, "ownerModule") == Client; }))
		{
			throw std::runtime_error(
			    "Empty UE host contexts require a complete matching generation without host units.");
		}
		return Results;
	}
	const auto UbtHash = Hash::HashBytes(UbtBytes);
	for (const auto& [Owner, Entry] : Contexts.items())
	{
		const fs::path Context = Path::ResolvePath(ContextSet.parent_path(), Entry.get<std::string>());
		const auto Recipe = ContextDocument::ReadJson(Context);
		if (ContextDocument::Text(Recipe, "ownerModule") != Owner ||
		    Hash::HashBytes(Recipe.dump()) != ContextDocument::Text(ContextHashes, Owner))
		{
			throw std::runtime_error("UE finalization context does not match the generation: " + Owner);
		}
		const char* ClientABIKey = Recipe.contains("clientABI") ? "clientABI"
		                                                        : "clientA"
		                                                          "bi";
		if (Recipe.value(ClientABIKey, "") == "ContextHandlesV1" && !Recipe.contains("owningBinary"))
		{
			continue;
		}
		const Request Request = BuildInputs::ReadRequest(Context, Manifest);
		const BuildLock Library(Request.Library);
		const Json Plan = HostPlan::CurrentHostPlan(Request);
		if (!Request.bPeLinkWitness)
		{
			throw std::runtime_error("UE finalization requires PeExportV1 link verification.");
		}
		UeTargetReceipt::RequireUeBinary(Request, Target, Project.parent_path());
		const auto HostBytes = File::ReadFile(Request.Receipt);
		const auto Host = Json::parse(HostBytes);
		const fs::path LinkPath = fs::path(Request.Library).concat(".link.json");
		const fs::path LinkedPath = fs::path(Request.Library).concat(".linked.json");
		Json Binary = Request.OwningBinary;
		Binary["ownerModules"] = {Owner};
		Binary["contentHash"] = Hash::HashFile(Request.BinaryPath);
		const Json Record = {{"format", "UMF.ModHostLinkReceipt"},
		                     {"schemaVersion", 1},
		                     {"status", "Succeeded"},
		                     {"target", Request.Context.at("target")},
		                     {"binary", Binary},
		                     {"ueTargetReceipt", {{"path", Path::PathToUtf8(UbtReceipt)}, {"contentHash", UbtHash}}},
		                     {"linkWitness", ContextDocument::Text(Host, "linkWitness")},
		                     {"hostLibraries", Json::array({{{"ownerModule", Owner},
		                                                     {"path", Path::PathToUtf8(Request.Library)},
		                                                     {"contentHash", Host.at("libraryHash")},
		                                                     {"hostReceiptHash", Hash::HashBytes(HostBytes)},
		                                                     {"contextHash", Request.ContextHash},
		                                                     {"generationId", Request.GenerationId},
		                                                     {"registrationAnchor", Request.Anchor},
		                                                     {"anchorRetained", true}}})}};
		const std::string LinkBytes = Record.dump(2) + '\n';
		const Json Linked =
		    LinkReceiptVerifier::VerifyLink(Request, Plan, LinkPath, Record, Hash::HashBytes(LinkBytes));
		if (Hash::HashFile(UbtReceipt) != UbtHash)
		{
			throw std::runtime_error("InputsChanged: the UBT target receipt changed during finalization.");
		}
		File::WriteFileAtomically(LinkPath, LinkBytes);
		File::WriteFileAtomically(LinkedPath, Linked.dump(2) + '\n');
		Results.push_back({{"ownerModule", Owner},
		                   {"binary", Request.OwningBinary},
		                   {"linkedArtifacts", Path::PathToUtf8(LinkedPath)}});
	}
	if (Results.empty())
	{
		throw std::runtime_error("UE finalization found no owning modules.");
	}
	return Results;
}
} // namespace Mod::BuildTool
