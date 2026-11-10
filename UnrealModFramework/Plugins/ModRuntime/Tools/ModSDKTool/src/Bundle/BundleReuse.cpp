// Copyright (c) 2026 Nelaric Contributors
#include "BundleReuse.h"

#include "Common/Files/Path.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Build/SdkInputCheck.h"
#include "ModSDKTool/src/Bundle/SdkBundle.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

Json BundleReuse::ReuseBundle(const SdkRequest& Request)
{
	const Json Existing = JsonDocument::ReadJson(Request.Output / "Manifest.json");
	JsonDocument::RequireFormat(Existing, "UMF.ModSdkBundle", {1, 2});
	if (Existing.at("gameId") != Request.Document.at("gameId") ||
	    Existing.at("sdkVersion") != Request.Document.at("sdkVersion") ||
	    Existing.at("pluginName") != Request.Document.at("pluginName"))
	{
		throw std::runtime_error("Refusing to replace an unrelated output directory.");
	}
	if (Existing.at("inputFingerprint") == Request.Fingerprint)
	{
		SdkBundle::InspectSdk(Request.Output);
		SdkInputCheck::CheckInputs(Request);
		BuildProgress::ReportStage("UpToDate", "The complete SDK bundle is current.");
		return {{"format", "UMF.SdkResult"},
		        {"schemaVersion", 1},
		        {"status", "UpToDate"},
		        {"bundle", Mod::BuildTool::Path::PathToUtf8(Request.Output)},
		        {"inputFingerprint", Request.Fingerprint},
		        {"capabilities", Existing.at("capabilities")}};
	}
	throw std::runtime_error("Published SDK inputs changed; select a new versioned output directory.");
}
} // namespace Mod::SdkTool
