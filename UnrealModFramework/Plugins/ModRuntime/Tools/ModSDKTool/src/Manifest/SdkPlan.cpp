// Copyright (c) 2026 Nelaric Contributors
#include "SdkPlan.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string SdkPlan::FingerprintPlan(const SdkRequest& Request)
{
	const Json Identity = {{"request", Request.Document},
	                       {"contract", Request.Contract},
	                       {"inputs", Request.Inputs},
	                       {"targets", Request.Targets}};
	return Mod::BuildTool::Hash::HashBytes(Identity.dump());
}

Json SdkPlan::MakeSdkPlan(const SdkRequest& Request)
{
	return {{"format", "UMF.SdkPlan"},
	        {"schemaVersion", 2},
	        {"status", "Planned"},
	        {"gameId", Request.Document.at("gameId")},
	        {"sdkVersion", Request.Document.at("sdkVersion")},
	        {"pluginName", Request.Document.at("pluginName")},
	        {"inputFingerprint", Request.Fingerprint},
	        {"outputDirectory", Mod::BuildTool::Path::PathToUtf8(Request.Output)},
	        {"editor", Request.Document.at("editor")},
	        {"contract", Request.Contract},
	        {"targets", Request.Targets},
	        {"publicHeaders", Request.Headers},
	        {"connectionDescriptor", Request.ConnectionArtifact},
	        {"hostFinalLinkVerified", false},
	        {"runtimeBindingVerified", false},
	        {"stages", {"GenerateSdk", "BuildSdkPlugin", "BuildDevelopmentPlugin", "Publish"}}};
}
} // namespace Mod::SdkTool
