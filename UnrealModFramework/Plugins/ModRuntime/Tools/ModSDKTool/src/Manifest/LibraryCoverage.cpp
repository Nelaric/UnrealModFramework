// Copyright (c) 2026 Nelaric Contributors
#include "LibraryCoverage.h"

#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Manifest/ContractValidator.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/PortableNames.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

LibraryCoverage::LibraryCoverage(SdkRequest& InRequest) : Request(InRequest)
{
}

void LibraryCoverage::Add(const Library& Job)
{
	if (!Outputs.insert(PortableNames::Fold(JsonDocument::Text(Job.Plan, "library"))).second)
	{
		throw std::runtime_error("Two SDK jobs write the same native library.");
	}
	CollectPublicArtifacts(Job);
	CollectBindings(Job);
}

void LibraryCoverage::CollectPublicArtifacts(const Library& Job)
{
	for (const Json& Artifact : Job.Plan.value("publicArtifacts", Json::array()))
	{
		if (Artifact.at("role") == "Contract" &&
		    Artifact.at("contentHash") == Mod::BuildTool::Hash::HashFile(Request.ContractFile))
		{
			bContractDeclared = true;
		}
		if (Job.Role == "Client" && Artifact.at("role") == "SdkHeader")
		{
			CollectPublicHeader(Artifact);
		}
		if (Job.Role == "Client" && Artifact.at("role") == "Contract" &&
		    Request.Contract.contains("connectionDescriptor") &&
		    Artifact.at("relativePath") == Request.Contract.at("connectionDescriptor"))
		{
			if (!Request.ConnectionArtifact.is_null() &&
			    Request.ConnectionArtifact.at("contentHash") != Artifact.at("contentHash"))
			{
				throw std::runtime_error("Conflicting native connection descriptors across client profiles.");
			}
			Request.ConnectionArtifact = Artifact;
		}
		if (Job.Role == "Client" && Artifact.at("role") == "SdkResource" && Request.Contract.contains("modLifecycle") &&
		    Artifact.at("relativePath") == Request.Contract.at("modLifecycle").at("resource"))
		{
			if (Artifact.at("contentHash") != Request.Contract.at("modLifecycle").at("resourceHash"))
			{
				throw std::runtime_error("The Mod lifecycle export differs across SDK profiles.");
			}
			Request.LifecycleArtifact = Artifact;
		}
	}
}

void LibraryCoverage::CollectPublicHeader(const Json& Artifact)
{
	const std::string Include = JsonDocument::Text(Artifact, "relativePath");
	PortableNames::PortablePath(Include);
	const std::string Key = PortableNames::Fold(Include);
	if (Headers.contains(Key) && (Headers.at(Key).at("contentHash") != Artifact.at("contentHash") ||
	                              Headers.at(Key).at("relativePath") != Artifact.at("relativePath")))
	{
		throw std::runtime_error("Conflicting public headers across SDK profiles: " + Include);
	}
	Headers[Key] = Artifact;
}

void LibraryCoverage::CollectBindings(const Library& Job)
{
	const Json& Target = Job.Plan.at("target");
	const std::string Profile = JsonDocument::Text(Target, "platform") + "/" +
	                            JsonDocument::Text(Target, "architecture") + "/" +
	                            JsonDocument::Text(Target, "configuration") + "/" + JsonDocument::Text(Target, "type");
	const bool bClient = Job.Role == "Client";
	if (bClient)
	{
		PortableNames::Identifier(JsonDocument::Text(Job.Plan, "ownerModule"));
		if (!ClientDestinations.insert(PortableNames::Fold(Profile + "/" + JsonDocument::Text(Job.Plan, "ownerModule")))
		         .second)
		{
			throw std::runtime_error("Duplicate public client library for the same target profile.");
		}
	}
	else
	{
		bHasHost = true;
	}
	RegisterBindings(Job, Profile, bClient);
	if (bClient)
	{
		bHasEditorClient |= Target.at("platform") == Request.Document.at("editor").at("platform") &&
		                    Target.at("architecture") == Request.Document.at("editor").at("architecture") &&
		                    Target.at("configuration") == "Development" && Target.at("type") == "Editor";
	}
}

void LibraryCoverage::RegisterBindings(const Library& Job, const std::string& Profile, bool bClient)
{
	auto& Bindings = bClient ? ClientBindings[Profile] : HostBindings[Profile];
	for (const Json& Binding : Job.Plan.value("requiredBindings", Json::array()))
	{
		for (const Json& API : Request.Contract.at("apis"))
		{
			const auto Id = JsonDocument::Text(API, "id");
			if (Binding.value("exportId", "") == Id &&
			    (!bClient || Binding.value("symbol", "") == JsonDocument::Text(API.at("native"), "symbol")) &&
			    Binding.value("signatureHash", "") == JsonDocument::Text(API, "signatureHash") &&
			    Binding.value("projectionHash", "") == JsonDocument::Text(API, "projectionHash") &&
			    !Bindings.insert(Id).second)
			{
				throw std::runtime_error(
				    std::string(bClient ? "Duplicate client endpoint for " : "Duplicate host binding for ") + Id +
				    " in " + Profile);
			}
		}
	}
}

void LibraryCoverage::ValidateBindings() const
{
	for (const Json& API : Request.Contract.at("apis"))
	{
		if (!Headers.contains(PortableNames::Fold(JsonDocument::Text(API.at("native"), "header"))))
		{
			throw std::runtime_error("Missing public header for " + JsonDocument::Text(API, "id"));
		}
		for (const auto& [Profile, Bindings] : HostBindings)
		{
			if (!Bindings.contains(JsonDocument::Text(API, "id")))
			{
				throw std::runtime_error("Missing checked host binding for " + JsonDocument::Text(API, "id") +
				                         " in profile " + Profile);
			}
		}
		for (const auto& [Profile, Bindings] : ClientBindings)
		{
			if (!Bindings.contains(JsonDocument::Text(API, "id")))
			{
				throw std::runtime_error("Missing checked client endpoint for " + JsonDocument::Text(API, "id") +
				                         " in profile " + Profile);
			}
		}
	}
}

void LibraryCoverage::Finalize()
{
	if (!bHasHost || !bHasEditorClient || !bContractDeclared)
	{
		throw std::runtime_error(
		    "SDK requires Host jobs, an author Editor Client job, and a declared public contract.");
	}
	if (Request.Contract.contains("modLifecycle") && Request.LifecycleArtifact.is_null())
	{
		throw std::runtime_error("Missing the declared Mod lifecycle export in the generated artifacts.");
	}
	ContractValidator::ValidateConnectionDescriptor(Request);
	if (Request.Contract.contains("errorHeader"))
	{
		const auto Path = JsonDocument::Text(Request.Contract, "errorHeader");
		const auto Header = Headers.find(PortableNames::Fold(Path));
		if (Header == Headers.end() || Header->second.at("relativePath") != Path)
		{
			throw std::runtime_error("Missing declared public SDK error header: " + Path);
		}
	}
	for (const auto& [Name, Header] : Headers)
	{
		Request.Headers.push_back(Header);
	}
	ValidateBindings();
}
} // namespace Mod::SdkTool
