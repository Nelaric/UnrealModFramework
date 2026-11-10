// Copyright (c) 2026 Nelaric Contributors
#include "LinkReceiptVerifier.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Binary/BinaryInspector.h"
#include "ModBuildTool/src/Build/BuildInputs.h"
#include "ModBuildTool/src/Build/ContextDocument.h"
#include "ModBuildTool/src/Build/HostPlan.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json LinkReceiptVerifier::VerifyLink(const Request& Request, const Json& Plan, const fs::path& LinkPath,
                                     const Json& Record, const std::string& LinkHash)
{
	ContextDocument::RequireFormat(Record, "UMF.ModHostLinkReceipt");
	if (Record.value("status", "") != "Succeeded" || Record.at("target") != Request.Context.at("target"))
	{
		throw std::runtime_error("The link record is unsuccessful or belongs to another Target.");
	}
	const Json& Binary = Record.at("binary");
	const fs::path BinaryPath = Path::ResolvePath(LinkPath.parent_path(), ContextDocument::Text(Binary, "path"));
	if (ContextDocument::Text(Binary, "id") != ContextDocument::Text(Request.OwningBinary, "id") ||
	    ContextDocument::Text(Binary, "kind") != ContextDocument::Text(Request.OwningBinary, "kind") ||
	    !fs::equivalent(BinaryPath, Request.BinaryPath))
	{
		throw std::runtime_error("The linked image is not the context's owning binary.");
	}
	const Json& Modules = Binary.at("ownerModules");
	if (!Modules.is_array() || Modules.empty())
	{
		throw std::runtime_error("The link record must identify its owning modules.");
	}
	std::set<std::string> SeenModules;
	for (const Json& Module : Modules)
	{
		const std::string Name = Module.get<std::string>();
		if (Name.empty() || !SeenModules.insert(Name).second)
		{
			throw std::runtime_error("Invalid or duplicate owning module in link record.");
		}
	}
	if (!SeenModules.contains(Request.Owner))
	{
		throw std::runtime_error("The linked binary does not own this host module.");
	}
	const Json& Bindings = Record.at("hostLibraries");
	if (!Bindings.is_array())
	{
		throw std::runtime_error("hostLibraries must be an array.");
	}
	const Json* Binding = nullptr;
	std::set<std::string> SeenLibraries;
	for (const Json& Candidate : Bindings)
	{
		const std::string Owner = ContextDocument::Text(Candidate, "ownerModule");
		if (!SeenLibraries.insert(Owner).second)
		{
			throw std::runtime_error("Duplicate host library owner in link record: " + Owner);
		}
		if (Owner == Request.Owner)
		{
			Binding = &Candidate;
		}
	}
	if (Binding == nullptr)
	{
		throw std::runtime_error("The successful link record does not include this host archive.");
	}
	const std::string HostReceiptHash = Hash::HashFile(Request.Receipt);
	const Json HostReceipt = ContextDocument::ReadJson(Request.Receipt);
	const fs::path LinkedLibrary = Path::ResolvePath(LinkPath.parent_path(), ContextDocument::Text(*Binding, "path"));
	if (!fs::equivalent(LinkedLibrary, Request.Library) ||
	    Binding->at("contentHash") != HostReceipt.at("libraryHash") ||
	    ContextDocument::Text(*Binding, "hostReceiptHash") != HostReceiptHash ||
	    ContextDocument::Text(*Binding, "contextHash") != Request.ContextHash ||
	    ContextDocument::Text(*Binding, "generationId") != Request.GenerationId ||
	    ContextDocument::Text(*Binding, "registrationAnchor") != Request.Anchor ||
	    !Binding->at("anchorRetained").get<bool>())
	{
		throw std::runtime_error("Linked archive identity, receipt, batch, or anchor retention does not match.");
	}
	const std::string BinaryHash = ContextDocument::Text(Binary, "contentHash");
	Hash::RequireDigest(BinaryHash, "binary.contentHash");
	const std::string Witness = Request.bPeLinkWitness ? ContextDocument::Text(HostReceipt, "linkWitness") : "";
	if (Request.bPeLinkWitness && Record.value("linkWitness", Witness) != Witness)
	{
		throw std::runtime_error("The link record names a different PE link witness.");
	}
	BinaryInspector::ValidateHostBinary(BinaryPath, Request.Platform, Request.Architecture,
	                                    ContextDocument::Text(Binary, "kind"), Witness);
	if (Hash::HashFile(BinaryPath) != BinaryHash ||
	    Hash::HashFile(Request.Library) != ContextDocument::Text(HostReceipt, "libraryHash") ||
	    Hash::HashFile(Request.Receipt) != HostReceiptHash ||
	    BuildInputs::ReadRequest(Request.ContextPath, Request.ManifestPath).Inputs != Request.Inputs)
	{
		throw std::runtime_error("InputsChanged: linked artifacts or their input records changed during finalization.");
	}
	const Json CurrentPlan = HostPlan::MakePlan(Request);
	if (!CurrentPlan.at("upToDate").get<bool>() || CurrentPlan.at("inputFingerprint") != Plan.at("inputFingerprint"))
	{
		throw std::runtime_error("InputsChanged: compiler dependencies changed during finalization.");
	}
	Json Result = Plan;
	Result["format"] = "UMF.ModHostLinkedArtifacts";
	Result["status"] = "Linked";
	Result["binary"] = {{"id", ContextDocument::Text(Binary, "id")},
	                    {"path", Path::PathToUtf8(BinaryPath)},
	                    {"kind", ContextDocument::Text(Binary, "kind")},
	                    {"contentHash", BinaryHash},
	                    {"ownerModules", Modules}};
	Result["libraryHash"] = HostReceipt.at("libraryHash");
	Result["hostBuildReceipt"] = {{"path", Path::PathToUtf8(Request.Receipt)}, {"contentHash", HostReceiptHash}};
	Result["linkReceipt"] = {{"path", Path::PathToUtf8(LinkPath)}, {"contentHash", LinkHash}};
	return Result;
}
} // namespace Mod::BuildTool
