// Copyright (c) 2026 Nelaric Contributors
#include "PluginSources.h"

#include "Common/Files/Path.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Build/StageFiles.h"
#include "ModSDKTool/src/Bundle/ClientLayout.h"
#include "ModSDKTool/src/Generation/SdkPluginGenerator.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/Library.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void PluginSources::PrepareSdkSource(const SdkRequest& Request, const fs::path& Source)
{
	BuildProgress::ReportStage("GenerateSdk", "Generating SDK source from the verified export contract.");
	SdkPluginGenerator::GenerateSdkPlugin(Request, Source);
	for (const Library& Job : Request.Libraries)
	{
		if (!Job.Plan.at("upToDate").get<bool>())
		{
			throw std::runtime_error("Native library was invalidated before SDK assembly.");
		}
		if (Job.Role == "Client")
		{
			StageFiles::CopyFile(Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Job.Plan, "library")),
			                     Source / "Source" / "ThirdParty" / "ModClient" / "Lib" /
			                         Mod::BuildTool::Path::PathFromUtf8(ClientLayout::ClientRelativePath(Job)));
		}
	}
}

void PluginSources::PrepareDevelopmentSource(const SdkRequest& Request, const fs::path& DevelopmentSource)
{
	const fs::path TemplateRoot = Request.DevelopmentPlugin.parent_path();
	for (const Json& Input : Request.Inputs)
	{
		const fs::path Path = Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Input, "path"));
		const fs::path Relative = Path.lexically_relative(TemplateRoot);
		if (!Relative.empty() && *Relative.begin() != ".." && *Relative.begin() != "Binaries" &&
		    *Relative.begin() != "Intermediate")
		{
			StageFiles::CopyFile(Path, DevelopmentSource / Relative);
		}
	}
}
} // namespace Mod::SdkTool
