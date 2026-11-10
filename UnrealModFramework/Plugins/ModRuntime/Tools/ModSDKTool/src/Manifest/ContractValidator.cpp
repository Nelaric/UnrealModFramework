// Copyright (c) 2026 Nelaric Contributors
#include "ContractValidator.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Manifest/ContractFields.h"
#include "ModSDKTool/src/Manifest/ContractSchema.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/PortableNames.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void ContractValidator::ValidateContract(SdkRequest& Request)
{
	const Json& Contract = Request.Contract;
	JsonDocument::RequireFormat(Contract, "UMF.SdkContract", {1, 2});
	if (Contract.value("status", "") != "Complete" || Contract.value("unresolvedTypeCount", -1) != 0 ||
	    Contract.value(ContractFields::AdapterABIKey(Contract), "") != "ContextHandlesV1" ||
	    JsonDocument::Text(Contract, "gameId") != JsonDocument::Text(Request.Document, "gameId"))
	{
		throw std::runtime_error("SDK requires a Complete ContextHandlesV1 contract for the requested game.");
	}
	for (const char* Key : {"contractHash", "projectionHash", "nativeABIHash"})
	{
		Mod::BuildTool::Hash::RequireDigest(
		    JsonDocument::Text(Contract, ContractSchema::ContractIdentityKey(Contract, Key)), Key);
	}
	if (Contract.contains("connectionDescriptor"))
	{
		PortableNames::PortablePath(JsonDocument::Text(Contract, "connectionDescriptor"));
		if (JsonDocument::Text(Contract, "connectionDescriptor") == "Contract/SdkContract.json")
		{
			throw std::runtime_error("Connection descriptor conflicts with the published SDK contract path.");
		}
	}
	if (Contract.contains("errorCodeEncodingVersion") || Contract.contains("errorHeader"))
	{
		if (Contract.value("errorCodeEncodingVersion", 0) != 2 || !Contract.contains("errorHeader") ||
		    !Contract.contains("connectionDescriptor"))
		{
			throw std::runtime_error(
			    "Structured SDK error codes require the versioned error header and connection descriptor.");
		}
		PortableNames::PortablePath(JsonDocument::Text(Contract, "errorHeader"));
	}
	if (Contract.contains("modLifecycle"))
	{
		const Json& Lifecycle = Contract.at("modLifecycle");
		if (!Lifecycle.is_object() || Lifecycle.value("schemaVersion", 0) != 1 ||
		    JsonDocument::Text(Lifecycle, "module") != "ModLoader" ||
		    JsonDocument::Text(Lifecycle, "resource") != "Contract/ModLifecycle.json")
		{
			throw std::runtime_error("Unsupported global Mod lifecycle projection.");
		}
		Mod::BuildTool::Hash::RequireDigest(JsonDocument::Text(Lifecycle, "resourceHash"), "modLifecycle.resourceHash");
	}
	const Json& Types = Contract.at("types");
	const Json& APIs = Contract.at("apis");
	if (!Types.is_array() || Types.empty() || Types.size() > 128 || !APIs.is_array() || APIs.empty() ||
	    APIs.size() > 1024)
	{
		throw std::runtime_error("SDK types/APIs must be bounded, nonempty arrays.");
	}
	std::set<std::string> TypeIds;
	std::set<std::string> Names;
	const std::string Plugin = JsonDocument::Text(Request.Document, "pluginName");
	for (const std::string& Name :
	     {"F" + Plugin + "PlayerContext", "E" + Plugin + "CallOutcome", "U" + Plugin + "Library"})
	{
		Names.insert(PortableNames::Fold(Name));
	}
	for (const Json& Type : Types)
	{
		const std::string Name = JsonDocument::Text(Type, "cppName");
		PortableNames::Identifier(Name);
		if (Type.value("kind", "") != "ObjectReference" || Name.front() != 'F' ||
		    !TypeIds.insert(JsonDocument::Text(Type, "id")).second || !Names.insert(PortableNames::Fold(Name)).second)
		{
			throw std::runtime_error("SDK types must be unique public ObjectReference structures.");
		}
	}
	std::set<std::string> APIIds;
	std::set<std::string> Functions;
	bool bHasGetter = false;
	bool bHasMethod = false;
	bool bHasTypedMethod = false;
	for (const Json& API : APIs)
	{
		const std::string Id = JsonDocument::Text(API, "id");
		const std::string Name = JsonDocument::Text(API, "name");
		PortableNames::Identifier(Name);
		if (!APIIds.insert(Id).second || !Functions.insert(PortableNames::Fold(Name)).second ||
		    (API.value("scope", "") != "Player" && API.value("scope", "") != "World") ||
		    API.value("thread", "") != "GameThread" || API.value("failureRule", "") != "ReturnError")
		{
			throw std::runtime_error("Unsupported or duplicate SDK API: " + Id);
		}
		JsonDocument::Text(API, "displayName");
		Mod::BuildTool::Hash::RequireDigest(JsonDocument::Text(API, "signatureHash"), "API signatureHash");
		Mod::BuildTool::Hash::RequireDigest(JsonDocument::Text(API, "projectionHash"), "API projectionHash");
		const Json& Permissions = API.at("permissions");
		if (!Permissions.is_array() || Permissions.size() > 128)
		{
			throw std::runtime_error("API permissions must be a bounded array: " + Id);
		}
		std::set<std::string> SeenPermissions;
		for (const Json& Permission : Permissions)
		{
			if (!Permission.is_string() || Permission.get<std::string>().empty() ||
			    !SeenPermissions.insert(Permission.get<std::string>()).second)
			{
				throw std::runtime_error("Empty or duplicate permission: " + Id);
			}
		}
		const Json& Native = API.at("native");
		PortableNames::Identifier(JsonDocument::Text(Native, "symbol"));
		PortableNames::PortablePath(JsonDocument::Text(Native, "header"));
		const std::string Shape = JsonDocument::Text(API, "shape");
		if (Shape == "ObjectGetter")
		{
			if (!TypeIds.contains(JsonDocument::Text(API, "resultTypeId")) ||
			    API.value("provider", "") != "LocalPlayerSubsystem" || API.value("scope", "") != "Player" ||
			    API.value("nullResult", "") != "Error")
			{
				throw std::runtime_error("ObjectGetter needs a resolved player provider, type, and null policy.");
			}
			bHasGetter = true;
		}
		else if (Shape == "FloatMethod")
		{
			if (!TypeIds.contains(JsonDocument::Text(API, "targetTypeId")))
			{
				throw std::runtime_error("FloatMethod target is not a public object reference.");
			}
			const std::string Parameter = JsonDocument::Text(API, "parameterName");
			PortableNames::Identifier(Parameter);
			if (Parameter == "Target" || Parameter == "Outcome" || Parameter == "ErrorCode" || Parameter == "Value" ||
			    Parameter == "NativeValue" || Parameter == "Status")
			{
				throw std::runtime_error("Float parameter collides with an SDK adapter parameter.");
			}
			JsonDocument::Text(API, "parameterId");
			bHasMethod = true;
		}
		else if (Shape == "TypedMethod")
		{
			const bool bProvider = API.contains("provider");
			if (bProvider ? (API.value("provider", "") != "LocalPlayerSubsystem" ||
			                 API.value("scope", "") != "Player" || API.contains("targetTypeId"))
			              : !TypeIds.contains(JsonDocument::Text(API, "targetTypeId")))
			{
				throw std::runtime_error("TypedMethod requires a public target or player provider: " + Id);
			}
			static const std::set<std::string_view> Scalars = {"bool",  "float", "double", "int8",   "int16", "int32",
			                                                   "int64", "uint8", "uint16", "uint32", "uint64"};
			const Json& Parameters = API.at("parameters");
			if (!Parameters.is_array() || Parameters.size() > 64 || !API.at("blueprint").is_boolean())
			{
				throw std::runtime_error("Invalid TypedMethod parameters: " + Id);
			}
			for (const Json& Parameter : Parameters)
			{
				const std::string Kind = JsonDocument::Text(Parameter, "kind");
				if (Kind == "ObjectInput")
				{
					PortableNames::Identifier(JsonDocument::Text(Parameter, "class"));
					PortableNames::Identifier(JsonDocument::Text(Parameter, "module"));
					PortableNames::PortablePath(JsonDocument::Text(Parameter, "include"));
					if (!Parameter.at("const").is_boolean())
					{
						throw std::runtime_error("Invalid object parameter qualification: " + Id);
					}
				}
				else if (Kind != "Scalar" || !Scalars.contains(JsonDocument::Text(Parameter, "cppType")))
				{
					throw std::runtime_error("Unsupported TypedMethod parameter: " + Id);
				}
			}
			const Json& Result = API.at("result");
			const std::string Kind = JsonDocument::Text(Result, "kind");
			if (Kind != "Void" && (Kind == "ObjectReference"
			                           ? !TypeIds.contains(JsonDocument::Text(Result, "typeId"))
			                           : Kind != "Scalar" || !Scalars.contains(JsonDocument::Text(Result, "cppType"))))
			{
				throw std::runtime_error("Unsupported TypedMethod result: " + Id);
			}
			bHasTypedMethod = true;
		}
		else
		{
			throw std::runtime_error("Unsupported projected API shape: " + Shape);
		}
	}
	if (!bHasTypedMethod && (!bHasGetter || !bHasMethod))
	{
		throw std::runtime_error("This SDK adapter requires an object getter and a float instance method.");
	}
	for (const Json& API : APIs)
	{
		if (API.at("shape") == "FloatMethod")
		{
			const bool bReachable = std::any_of(APIs.begin(), APIs.end(),
			                                    [&](const Json& Getter) {
				                                    return Getter.at("shape") == "ObjectGetter" &&
				                                           Getter.at("resultTypeId") == API.at("targetTypeId");
			                                    });
			if (!bReachable)
			{
				throw std::runtime_error("No getter returns the method's public target type: " +
				                         JsonDocument::Text(API, "id"));
			}
		}
	}
}

void ContractValidator::ValidateConnectionDescriptor(const SdkRequest& Request)
{
	if (!Request.Contract.contains("connectionDescriptor"))
	{
		return;
	}
	if (Request.ConnectionArtifact.is_null())
	{
		throw std::runtime_error("Missing declared public native connection descriptor.");
	}
	const fs::path Path =
	    Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Request.ConnectionArtifact, "sourcePath"));
	if (Mod::BuildTool::Hash::HashFile(Path) != JsonDocument::Text(Request.ConnectionArtifact, "contentHash"))
	{
		throw std::runtime_error("Native connection descriptor changed during planning.");
	}
	const Json Connection = JsonDocument::ReadJson(Path);
	ContractSchema::RequireClientABIFormat(Connection);
	if (Connection.value("status", "") != "Complete" || Connection.value("abiVersion", 0) != 1 ||
	    Connection.value(ContractFields::AdapterABIKey(Connection), "") != "ContextHandlesV1" ||
	    Connection.value("errorCodeEncodingVersion", 1) != Request.Contract.value("errorCodeEncodingVersion", 1) ||
	    Connection.at("gameId") != Request.Contract.at("gameId"))
	{
		throw std::runtime_error("Native connection descriptor does not match the SDK contract.");
	}
	for (const char* Key : {"contractHash", "projectionHash", "nativeABIHash"})
	{
		if (Connection.at(ContractSchema::ContractIdentityKey(Connection, Key)) !=
		    Request.Contract.at(ContractSchema::ContractIdentityKey(Request.Contract, Key)))
		{
			throw std::runtime_error(std::string("Native connection descriptor mismatch: ") + Key);
		}
	}
	for (const Json& API : Request.Contract.at("apis"))
	{
		if (!std::any_of(Connection.at("apis").begin(), Connection.at("apis").end(),
		                 [&](const Json& Entry)
		                 {
			                 return Entry.at("id") == API.at("id") &&
			                        Entry.at("symbol") == API.at("native").at("symbol") &&
			                        Entry.at("signatureHash") == API.at("signatureHash") &&
			                        Entry.at("projectionHash") == API.at("projectionHash");
		                 }))
		{
			throw std::runtime_error("Native connection descriptor is missing a matching API: " +
			                         JsonDocument::Text(API, "id"));
		}
	}
}
} // namespace Mod::SdkTool
