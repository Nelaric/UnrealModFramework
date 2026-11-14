// Copyright (c) 2026 Nelaric Contributors
#include "SdkBundle.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Bundle/BundleInventory.h"
#include "ModSDKTool/src/Manifest/ContractSchema.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json SdkBundle::InspectSdk(const fs::path& Directory)
{
	using namespace Mod::BuildTool;
	const fs::path Root = fs::canonical(Directory);
	const Json Manifest = JsonDocument::ReadJson(Root / "Manifest.json");
	JsonDocument::RequireFormat(Manifest, "UMF.ModSdkBundle", {1, 2});
	const Json& Files = Manifest.at("files");
	if (!Files.is_array() || Files.empty() || Files.size() > 100000)
	{
		throw std::runtime_error("Bundle file inventory is empty or oversized.");
	}
	Mod::BuildTool::Hash::RequireDigest(JsonDocument::Text(Manifest, "inputFingerprint"), "inputFingerprint");
	std::set<std::string> Seen;
	for (const Json& File : Files)
	{
		const std::string Relative = JsonDocument::Text(File, "path");
		const fs::path Path = Mod::BuildTool::Path::ResolveGeneratedPath(Root, Relative);
		Mod::BuildTool::Hash::RequireDigest(JsonDocument::Text(File, "contentHash"), "file.contentHash");
		if (Path == Root / "Manifest.json" || !Seen.insert(Relative).second ||
		    Mod::BuildTool::Hash::HashFile(Path) != JsonDocument::Text(File, "contentHash"))
		{
			throw std::runtime_error("Bundle hash mismatch, self-reference, or duplicate file: " + Relative);
		}
	}
	if (BundleInventory::Inventory(Root) != Files)
	{
		throw std::runtime_error("Bundle inventory does not exactly match the published files.");
	}
	const Json Contract = JsonDocument::ReadJson(Root / "Contract" / "SdkContract.json");
	JsonDocument::RequireFormat(Contract, "UMF.SdkContract", {1, 2});
	if (Contract.contains("errorHeader"))
	{
		const auto Path = JsonDocument::Text(Contract, "errorHeader");
		const auto NativeHeader = Mod::BuildTool::Path::ResolveGeneratedPath(Root, "Native/Include/" + Path);
		const auto PluginHeader =
		    Mod::BuildTool::Path::ResolveGeneratedPath(Root, "Plugins/" + JsonDocument::Text(Manifest, "pluginName") +
		                                                         "/Source/ThirdParty/ModClient/Include/" + Path);
		if (Mod::BuildTool::Hash::HashFile(NativeHeader) != Mod::BuildTool::Hash::HashFile(PluginHeader))
		{
			throw std::runtime_error("The bundle and SDK plugin need the same public error header.");
		}
	}
	if (Contract.contains("modLifecycle"))
	{
		const Json& Lifecycle = Contract.at("modLifecycle");
		const std::string Resource = JsonDocument::Text(Lifecycle, "resource");
		const std::string PluginRoot = "Plugins/" + JsonDocument::Text(Manifest, "pluginName") + "/";
		const std::string Digest = JsonDocument::Text(Lifecycle, "resourceHash");
		if (Mod::BuildTool::Hash::HashFile(Mod::BuildTool::Path::ResolveGeneratedPath(Root, Resource)) != Digest ||
		    Mod::BuildTool::Hash::HashFile(Mod::BuildTool::Path::ResolveGeneratedPath(Root, PluginRoot + Resource)) !=
		        Digest)
		{
			throw std::runtime_error("The SDK lifecycle export does not match the contract.");
		}
		std::vector<const char*> Headers{"ModInstance.h", "ModInstanceSubSystem.h"};
		if (Lifecycle.value("schemaVersion", 0) == 2)
		{
			Headers.push_back("ModExtensionLibrary.h");
		}
		for (const char* Header : Headers)
		{
			if (!fs::is_regular_file(
			        Mod::BuildTool::Path::ResolveGeneratedPath(Root, PluginRoot + "Source/ModLoader/Public/" + Header)))
			{
				throw std::runtime_error(std::string("Missing generated SDK lifecycle header: ") + Header);
			}
		}
	}
	if (Contract.contains("connectionDescriptor"))
	{
		const std::string Relative = JsonDocument::Text(Contract, "connectionDescriptor");
		const fs::path Descriptor = Mod::BuildTool::Path::ResolveGeneratedPath(Root, Relative);
		const fs::path PluginCopy = Mod::BuildTool::Path::ResolveGeneratedPath(
		    Root, "Plugins/" + JsonDocument::Text(Manifest, "pluginName") + "/" + Relative);
		const Json Connection = JsonDocument::ReadJson(Descriptor);
		ContractSchema::RequireClientABIFormat(Connection);
		if (Mod::BuildTool::Hash::HashFile(Descriptor) != Mod::BuildTool::Hash::HashFile(PluginCopy) ||
		    Connection.at("gameId") != Manifest.at("gameId") ||
		    Connection.value("errorCodeEncodingVersion", 1) != Contract.value("errorCodeEncodingVersion", 1))
		{
			throw std::runtime_error("The bundle and SDK plugin need the same native connection descriptor.");
		}
		for (const char* Key : {"contractHash", "projectionHash", "nativeABIHash"})
		{
			const Json& ContractValue = Contract.at(ContractSchema::ContractIdentityKey(Contract, Key));
			if (Connection.at(ContractSchema::ContractIdentityKey(Connection, Key)) != ContractValue ||
			    Manifest.at(ContractSchema::ContractIdentityKey(Manifest, Key)) != ContractValue)
			{
				throw std::runtime_error(std::string("Published connection identity mismatch: ") + Key);
			}
		}
	}
	return {{"format", "UMF.SdkInspection"},
	        {"schemaVersion", 1},
	        {"valid", true},
	        {"bundle", Mod::BuildTool::Path::PathToUtf8(Root)},
	        {"gameId", Manifest.at("gameId")},
	        {"sdkVersion", Manifest.at("sdkVersion")},
	        {"inputFingerprint", Manifest.at("inputFingerprint")},
	        {"capabilities", Manifest.at("capabilities")}};
}
} // namespace Mod::SdkTool
