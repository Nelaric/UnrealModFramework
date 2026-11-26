// Copyright (c) 2026 Nelaric Contributors
#include "TargetArtifacts.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Build/PathRelations.h"
#include "ModSDKTool/src/Manifest/ContractSchema.h"
#include "ModSDKTool/src/Manifest/ContractValidator.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/PortableNames.h"
#include "ModSDKTool/src/Manifest/SdkInputs.h"

#include <map>
#include <set>
#include <stdexcept>

namespace Mod::SdkTool
{
namespace
{
using Mod::BuildTool::Hash;
using Mod::BuildTool::Path;

void CheckBindings(const Json& Batch, const Json& Descriptor)
{
	const std::string Client = JsonDocument::Text(Descriptor, "clientOwnerModule");
	std::set<std::string> Expected;
	for (const Json& API : Descriptor.at("apis"))
	{
		for (const bool Host : {true, false})
		{
			const Json Binding = {{"ownerModule", Host ? API.at("ownerModule") : Json(Client)},
			                      {"symbol", API.at(Host ? "hostSymbol" : "symbol")},
			                      {"exportId", API.at("id")},
			                      {"signatureHash", API.at("signatureHash")},
			                      {"projectionHash", API.at("projectionHash")}};
			if (!Expected.insert(Binding.dump()).second)
			{
				throw std::runtime_error("Duplicate declared SDK binding: " + Binding.dump());
			}
		}
	}
	for (const Json& Connection : Descriptor.at("connections"))
	{
		for (const std::string Key : {"querySymbol", "connectSymbol", "disconnectSymbol", "releaseObjectSymbol"})
		{
			const Json Binding = {{"ownerModule", Key == "querySymbol" ? Connection.at("ownerModule") : Json(Client)},
			                      {"symbol", Connection.at(Key)}};
			if (!Expected.insert(Binding.dump()).second)
			{
				throw std::runtime_error("Duplicate generated connection: " + Binding.dump());
			}
		}
	}
	for (const Json& Binding : Batch.at("requiredSymbols"))
	{
		if (!Expected.erase(Binding.dump()))
		{
			throw std::runtime_error("Unexpected or duplicate target binding: " + Binding.dump());
		}
	}
	if (!Expected.empty())
	{
		throw std::runtime_error("Missing target binding: " + *Expected.begin());
	}
}

struct PublicInputs
{
	std::map<std::string, Json> Files;
	Json Client;
	Json Connection;
	Json Lifecycle;
	Json Identity = Json::object();
};

PublicInputs ReadBatch(SdkRequest& Request, const fs::path& Manifest, const Json& Batch)
{
	PublicInputs Public;
	std::set<std::string> Seen;
	std::map<std::string, Json> Units;
	bool ContractDeclared = false;
	const std::string ClientOwner = JsonDocument::Text(Request.Document, "gameId") + "ModClient";
	const std::string ClientPath = "Client/" + ClientOwner + "/" + ClientOwner + ".modclient.cpp";
	for (const Json& Item : Batch.at("generatedFiles"))
	{
		const auto Relative = JsonDocument::Text(Item, "path");
		PortableNames::PortablePath(Relative);
		const auto File = Path::ResolveGeneratedPath(Manifest.parent_path(), Relative);
		const auto Digest = JsonDocument::Text(Item, "contentHash");
		if (!Seen.insert(PortableNames::Fold(Relative)).second || Hash::HashFile(File) != Digest)
		{
			throw std::runtime_error("Duplicate or changed generated input: " + Relative);
		}
		const auto Role = JsonDocument::Text(Item, "role");
		if (Role == "ClientSource" || Role == "HostBinding" || Role == "HostRegistration" ||
		    Role == "CompileVerification")
		{
			const std::string Owner = JsonDocument::Text(Item, "ownerModule");
			if ((Role == "ClientSource") != (Owner == ClientOwner && Relative == ClientPath))
			{
				throw std::runtime_error("Invalid generated source ownership: " + Relative);
			}
			Units.emplace(Relative, Json{{"source", Relative}, {"ownerModule", Owner}, {"role", Role}});
		}
		const Json Artifact = {{"role", Role},
		                       {"relativePath", Relative},
		                       {"sourcePath", Path::PathToUtf8(File)},
		                       {"contentHash", Digest}};
		if (Role == "ClientSource" && Relative == ClientPath && Item.at("ownerModule") == ClientOwner)
		{
			Public.Client = Artifact;
		}
		else if (Role == "SdkHeader" && Relative.starts_with("SDK/Public/") &&
		         (File.extension() == ".h" || File.extension() == ".hpp"))
		{
			Public.Files.emplace(Relative, Artifact);
		}
		else if (Role == "Contract" && Relative == Request.Contract.value("connectionDescriptor", ""))
		{
			Public.Connection = Artifact;
		}
		else if (Role == "SdkResource" && Request.Contract.contains("modLifecycle") &&
		         Request.Contract.at("modLifecycle").at("resource") == Relative &&
		         Request.Contract.at("modLifecycle").at("resourceHash") == Digest)
		{
			Public.Lifecycle = Artifact;
		}
		else if (Role == "Contract" && Digest == Hash::HashFile(Request.ContractFile))
		{
			ContractDeclared = true;
		}
		else
		{
			continue;
		}
		SdkInputs::AddInput(Request, File);
		Public.Identity[Relative] = Digest;
	}
	for (const auto& Unit : Batch.at("compilationUnits"))
	{
		const auto Found = Units.find(JsonDocument::Text(Unit, "source"));
		if (Found == Units.end() || Found->second != Unit)
		{
			throw std::runtime_error("Invalid or duplicate UBT compilation unit: " + Unit.dump());
		}
		Units.erase(Found);
	}
	if (!Units.empty())
	{
		throw std::runtime_error("Missing UBT compilation unit: " + Units.begin()->first);
	}
	if (!ContractDeclared || Public.Client.is_null() || Public.Connection.is_null() ||
	    (Request.Contract.contains("modLifecycle") && Public.Lifecycle.is_null()))
	{
		throw std::runtime_error(
		    "Missing declared contract, shared Client source, ABI descriptor or lifecycle export.");
	}
	for (const Json& API : Request.Contract.at("apis"))
	{
		if (!Public.Files.contains(JsonDocument::Text(API.at("native"), "header")))
		{
			throw std::runtime_error("Missing public SDK header for " + JsonDocument::Text(API, "id"));
		}
	}
	if (Request.Contract.contains("errorHeader") &&
	    !Public.Files.contains(JsonDocument::Text(Request.Contract, "errorHeader")))
	{
		throw std::runtime_error("Missing public SDK error header.");
	}
	CheckBindings(Batch,
	              JsonDocument::ReadJson(Path::PathFromUtf8(JsonDocument::Text(Public.Connection, "sourcePath"))));
	return Public;
}
} // namespace

void TargetArtifacts::ReadTargets(SdkRequest& Request)
{
	const Json& Targets = Request.Document.at("targets");
	if (!Targets.is_array() || Targets.empty() || Targets.size() > 256)
	{
		throw std::runtime_error("targets requires 1 to 256 UBT generations.");
	}
	std::set<std::string> Profiles;
	Json Identity;
	for (const Json& Target : Targets)
	{
		const auto Manifest = fs::canonical(
		    Path::ResolvePath(Request.File.parent_path(), JsonDocument::Text(Target, "generatedManifest")));
		if (PathRelations::IsInside(Request.Output, Manifest.parent_path()))
		{
			throw std::runtime_error("SDK output overlaps the generated input root.");
		}
		const Json Batch = JsonDocument::ReadJson(Manifest);
		JsonDocument::RequireFormat(Batch, "UMF.GeneratedArtifacts", {4});
		if (Batch.at("gameId") != Request.Document.at("gameId") || Batch.at("status") != "Complete" ||
		    Batch.at("unresolvedTypeCount") != 0 || Batch.at("outputLayout") != "Flat")
		{
			throw std::runtime_error("Expected a complete UBT generation for the requested game.");
		}
		Json Profile;
		for (const char* Field : {"target", "platform", "architecture", "configuration"})
		{
			Profile[Field] = JsonDocument::Text(Target, Field);
			if (Profile.at(Field) != Batch.at(Field))
			{
				throw std::runtime_error(std::string("Wrong UBT generation for ") + Field);
			}
		}
		if (!Profiles.insert(Profile.dump()).second)
		{
			throw std::runtime_error("Duplicate SDK target: " + Profile.dump());
		}
		for (const char* Field : {"contractHash", "projectionHash", "nativeABIHash"})
		{
			if (Batch.at(Field) != Request.Contract.at(ContractSchema::ContractIdentityKey(Request.Contract, Field)))
			{
				throw std::runtime_error(std::string("Mixed SDK target generations: ") + Field);
			}
		}
		const auto Public = ReadBatch(Request, Manifest, Batch);
		if (Identity.is_null())
		{
			Identity = Public.Identity;
			for (const auto& [Name, Header] : Public.Files)
			{
				Request.Headers.push_back(Header);
			}
			Request.ClientSource = Public.Client;
			Request.ConnectionArtifact = Public.Connection;
			Request.LifecycleArtifact = Public.Lifecycle;
			ContractValidator::ValidateConnectionDescriptor(Request);
		}
		else if (Identity != Public.Identity)
		{
			throw std::runtime_error("Conflicting public SDK artifacts across targets.");
		}
		Profile["generationId"] = JsonDocument::Text(Batch, "generationId");
		Hash::RequireDigest(Profile.at("generationId").get<std::string>(), "generationId");
		Profile["generatedManifest"] = Path::PathToUtf8(Manifest);
		Request.Targets.push_back(Profile);
		SdkInputs::AddInput(Request, Manifest);
	}
}
} // namespace Mod::SdkTool
