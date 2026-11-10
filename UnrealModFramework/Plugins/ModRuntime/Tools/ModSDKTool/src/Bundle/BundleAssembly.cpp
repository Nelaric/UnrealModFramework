// Copyright (c) 2026 Nelaric Contributors
#include "BundleAssembly.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Build/StageFiles.h"
#include "ModSDKTool/src/Bundle/BundleInventory.h"
#include "ModSDKTool/src/Bundle/ClientLayout.h"
#include "ModSDKTool/src/Manifest/ContractSchema.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/Library.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void BundleAssembly::CopySdkPackages(const SdkRequest& Request, const PluginPackages& Packages,
                                     const std::string& Plugin, const fs::path& Bundle)
{
	StageFiles::CopyTree(Packages.Sdk, Bundle / "Plugins" / Plugin);
	StageFiles::CopyTree(Packages.Development, Bundle / "Plugins" / Request.DevelopmentPlugin.stem());
	Mod::BuildTool::File::WriteFileAtomically(Bundle / "Contract" / "SdkContract.json",
	                                          Request.Contract.dump(2) + "\n");
	if (!Request.LifecycleArtifact.is_null())
	{
		const std::string Resource = JsonDocument::Text(Request.LifecycleArtifact, "relativePath");
		StageFiles::CopyFile(Mod::BuildTool::Path::ResolveGeneratedPath(Packages.Sdk, Resource),
		                     Mod::BuildTool::Path::ResolveGeneratedPath(Bundle, Resource));
	}
	if (!Request.ConnectionArtifact.is_null())
	{
		const fs::path SourcePath =
		    Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Request.ConnectionArtifact, "sourcePath"));
		if (Mod::BuildTool::Hash::HashFile(SourcePath) != JsonDocument::Text(Request.ConnectionArtifact, "contentHash"))
		{
			throw std::runtime_error("Native connection descriptor changed before SDK publication.");
		}
		StageFiles::CopyFile(SourcePath, Mod::BuildTool::Path::ResolveGeneratedPath(
		                                     Bundle, JsonDocument::Text(Request.ConnectionArtifact, "relativePath")));
	}
}

void BundleAssembly::CopySdkNativeArtifacts(const SdkRequest& Request, const fs::path& Bundle)
{
	for (const Json& Header : Request.Headers)
	{
		StageFiles::CopyFile(Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Header, "sourcePath")),
		                     Mod::BuildTool::Path::ResolveGeneratedPath(Bundle / "Native" / "Include",
		                                                                JsonDocument::Text(Header, "relativePath")));
	}
	for (const Library& Job : Request.Libraries)
	{
		if (Job.Role == "Client")
		{
			StageFiles::CopyFile(Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Job.Plan, "library")),
			                     Bundle / "Native" / "Lib" /
			                         Mod::BuildTool::Path::PathFromUtf8(ClientLayout::ClientRelativePath(Job)));
		}
	}
}

Json BundleAssembly::WriteBundleManifest(const SdkRequest& Request, const fs::path& Bundle, const std::string& Plugin)
{
	const Json Capabilities = {{"nativeLibrariesBuilt", true},     {"authorEditorBuilt", true},
	                           {"typedBlueprintCallsBuilt", true}, {"hostFinalLinkVerified", false},
	                           {"gameSdkRuntimeBuilt", false},     {"runtimeBindingVerified", false}};
	const Json Manifest = {
	    {"format", "UMF.ModSdkBundle"},
	    {"schemaVersion", 2},
	    {"toolVersion", "0.1.0"},
	    {"gameId", Request.Document.at("gameId")},
	    {"sdkVersion", Request.Document.at("sdkVersion")},
	    {"pluginName", Plugin},
	    {"inputFingerprint", Request.Fingerprint},
	    {"contractHash", Request.Contract.at("contractHash")},
	    {"projectionHash", Request.Contract.at("projectionHash")},
	    {"nativeABIHash", Request.Contract.at(ContractSchema::ContractIdentityKey(Request.Contract, "nativeABIHash"))},
	    {"editor", Request.Document.at("editor")},
	    {"capabilities", Capabilities},
	    {"files", BundleInventory::Inventory(Bundle)}};
	Mod::BuildTool::File::WriteFileAtomically(Bundle / "Manifest.json", Manifest.dump(2) + "\n");
	return Capabilities;
}
} // namespace Mod::SdkTool
