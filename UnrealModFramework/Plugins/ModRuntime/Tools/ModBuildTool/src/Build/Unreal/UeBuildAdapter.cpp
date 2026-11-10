// Copyright (c) 2026 Nelaric Contributors
#include "UeBuildAdapter.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/BuildInputs.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/HostPlan.h"
#include "ModBuildTool/src/Build/LinkReceiptVerifier.h"
#include "ModBuildTool/src/Build/Request.h"
#include "ModBuildTool/src/Build/Unreal/UeTargetReceipt.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void UeBuildAdapter::RefreshUeWatches(const fs::path& ContextSetPath)
{
	if (!fs::is_regular_file(ContextSetPath))
	{
		return;
	}
	const fs::path ContextSet = fs::canonical(ContextSetPath);
	const Json Pipeline = Json::parse(File::ReadFile(ContextSet), nullptr, false);
	if (!Pipeline.is_object() || !Pipeline.contains("generatedManifest"))
	{
		return;
	}
	const fs::path Manifest =
	    Path::ResolvePath(ContextSet.parent_path(), ContextDocument::Text(Pipeline, "generatedManifest"));
	if (!fs::is_regular_file(Manifest))
	{
		return;
	}
	const Json Batch = Json::parse(File::ReadFile(Manifest), nullptr, false);
	if (!Batch.is_object() || Batch.value("status", "") != "Complete" || !Batch.contains("watchedDirectories") ||
	    !Batch.contains("inputFiles"))
	{
		return;
	}
	fs::path Changed = BuildInputs::ChangedWatchedDirectory(Batch.at("watchedDirectories"));
	if (Changed.empty())
	{
		for (const auto& File : Batch.at("inputFiles"))
		{
			if (File.at("contentHash") == "")
			{
				const auto Path = Path::PathFromUtf8(ContextDocument::Text(File, "path"));
				if (fs::exists(Path))
				{
					Changed = Path;
					break;
				}
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
	const fs::path ContextSet = fs::canonical(ContextSetPath);
	const Json Pipeline = ContextDocument::ReadJson(ContextSet);
	ContextDocument::RequireFormat(Pipeline, "UMF.ModHostContextSet");
	const fs::path Manifest =
	    Path::ResolvePath(ContextSet.parent_path(), ContextDocument::Text(Pipeline, "generatedManifest"));
	const fs::path Project = Path::ResolvePath(ContextSet.parent_path(), ContextDocument::Text(Pipeline, "project"));
	const fs::path UbtReceipt = fs::canonical(TargetReceiptPath);
	const std::string UbtBytes = File::ReadFile(UbtReceipt);
	const Json Target = Json::parse(UbtBytes);
	if (Path::ResolvePath(UbtReceipt.parent_path(), ContextDocument::Text(Target, "Project")) != Project)
	{
		throw std::runtime_error("UBT target receipt belongs to a different project.");
	}
	const BuildLock Generation(Manifest.parent_path() / ".mht.lock", "");
	const auto ContextHashes = ContextDocument::ReadJson(Manifest).at("hostBuildContexts");
	const auto& Contexts = Pipeline.at("contexts");
	if (!Contexts.is_object() || Contexts.size() != ContextHashes.size())
	{
		throw std::runtime_error("UE finalization requires every generated producer context.");
	}
	Json Results = Json::array();
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
