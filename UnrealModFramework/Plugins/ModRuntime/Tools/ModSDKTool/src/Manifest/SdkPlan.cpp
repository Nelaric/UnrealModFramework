// Copyright (c) 2026 Nelaric Contributors
#include "SdkPlan.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/LibraryCoverage.h"
#include "ModSDKTool/src/Process/ToolProcess.h"

#include <cstddef>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json SdkPlan::ReadLibraryPlan(const SdkRequest& Request, const Library& Job, const fs::path& ResultFile)
{
	if (ToolProcess::RunTool(Request.Mbt,
	                         {"plan", "--context", Mod::BuildTool::Path::PathToUtf8(Job.Context),
	                          "--generated-manifest", Mod::BuildTool::Path::PathToUtf8(Job.Manifest), "--output",
	                          Mod::BuildTool::Path::PathToUtf8(ResultFile)},
	                         Request.Project.parent_path(), Request.Document.value("timeoutSeconds", 1800u)) != 0)
	{
		throw std::runtime_error("MBT rejected the library plan: " + Mod::BuildTool::Path::PathToUtf8(Job.Context));
	}
	auto Plan = JsonDocument::ReadJson(ResultFile);
	JsonDocument::RequireFormat(Plan, "UMF.ModHostPlan", {1, 2});
	return Plan;
}

Json SdkPlan::CollectLibraryPlans(SdkRequest& Request, const fs::path& Temporary)
{
	Request.Headers = Json::array();
	Request.ConnectionArtifact = nullptr;
	Request.LifecycleArtifact = nullptr;
	LibraryCoverage Coverage(Request);
	Json Jobs = Json::array();
	for (std::size_t Index = 0; Index < Request.Libraries.size(); ++Index)
	{
		Library& Job = Request.Libraries[Index];
		Job.Plan = ReadLibraryPlan(Request, Job, Temporary / (std::to_string(Index) + ".json"));
		Coverage.Add(Job);
		Jobs.push_back({{"role", Job.Role},
		                {"context", Mod::BuildTool::Path::PathToUtf8(Job.Context)},
		                {"generatedManifest", Mod::BuildTool::Path::PathToUtf8(Job.Manifest)},
		                {"mbtPlan", Job.Plan}});
	}
	Coverage.Finalize();
	return Jobs;
}

std::string SdkPlan::FingerprintPlan(const SdkRequest& Request, const Json& Jobs)
{
	const Json Identity = {{"toolVersion", "0.1.0"},
	                       {"request", Request.Document},
	                       {"contract", Request.Contract},
	                       {"inputs", Request.Inputs},
	                       {"libraries", Jobs}};
	// Cache status is observable state, not part of the SDK identity.
	Json Stable = Identity;
	for (Json& Job : Stable["libraries"])
	{
		Job["mbtPlan"].erase("upToDate");
		Job["mbtPlan"].erase("reason");
	}
	return Mod::BuildTool::Hash::HashBytes(Stable.dump());
}

Json SdkPlan::MakeSdkPlan(const SdkRequest& Request, const Json& Jobs)
{
	return {{"format", "UMF.SdkPlan"},
	        {"schemaVersion", 1},
	        {"status", "Planned"},
	        {"gameId", Request.Document.at("gameId")},
	        {"sdkVersion", Request.Document.at("sdkVersion")},
	        {"pluginName", Request.Document.at("pluginName")},
	        {"inputFingerprint", Request.Fingerprint},
	        {"outputDirectory", Mod::BuildTool::Path::PathToUtf8(Request.Output)},
	        {"editor", Request.Document.at("editor")},
	        {"contract", Request.Contract},
	        {"libraries", Jobs},
	        {"publicHeaders", Request.Headers},
	        {"connectionDescriptor", Request.ConnectionArtifact},
	        {"hostFinalLinkVerified", false},
	        {"runtimeBindingVerified", false},
	        {"stages", {"BuildLibraries", "GenerateSdk", "BuildSdkPlugin", "BuildDevelopmentPlugin", "Publish"}}};
}
} // namespace Mod::SdkTool
