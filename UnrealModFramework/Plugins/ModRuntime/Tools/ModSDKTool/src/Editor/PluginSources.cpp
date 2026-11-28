// Copyright (c) 2026 Nelaric Contributors
#include "PluginSources.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Build/StageFiles.h"
#include "ModSDKTool/src/Editor/DevelopmentEditorResources.h"
#include "ModSDKTool/src/Generation/SdkPluginGenerator.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void PluginSources::PrepareSdkSource(const SdkRequest& Request, const fs::path& Source, bool bGameHost)
{
	BuildProgress::ReportStage("GenerateSdk", "Generating SDK source from the verified export contract.");
	SdkPluginGenerator::GenerateSdkPlugin(Request, Source, bGameHost);
	const std::string Module = JsonDocument::Text(Request.Document, "pluginName") + "Runtime";
	const auto Client = Mod::BuildTool::Path::PathFromUtf8(JsonDocument::Text(Request.ClientSource, "sourcePath"));
	StageFiles::CopyFile(Client, Source / "Source" / Module / "Private" / Client.filename());
}

fs::path PluginSources::PrepareDevelopmentSource(const fs::path& Work)
{
	using namespace Mod::BuildTool;
	const fs::path Source = fs::canonical(Work) / DevelopmentPluginName;
	if (!fs::create_directory(Source))
	{
		throw std::runtime_error("Development editor source directory already exists: " + Path::PathToUtf8(Source));
	}
	for (const EmbeddedResource& Resource : GetDevelopmentEditorResources())
	{
		const fs::path Destination = Path::ResolveGeneratedPath(Source, Resource.RelativePath);
		const std::string_view Contents{reinterpret_cast<const char*>(Resource.Bytes), Resource.Size};
		File::WriteFileAtomically(Destination, Contents);
	}

	const Json Descriptor = {
	    {"FileVersion", 3},
	    {"Version", 3},
	    {"VersionName", "0.1.5"},
	    {"FriendlyName", "模组开发 SDK 编辑器"},
	    {"Description", "Independent mod development SDK editor for UnrealModFramework."},
	    {"Category", "Mod Framework"},
	    {"CanContainContent", false},
	    {"IsBetaVersion", true},
	    {"Installed", false},
	    {"EnabledByDefault", false},
	    {"Modules", Json::array({{{"Name", DevelopmentModuleName}, {"Type", "Editor"}, {"LoadingPhase", "Default"}}})}};
	const fs::path Plugin = Source / (std::string(DevelopmentPluginName) + ".uplugin");
	File::WriteFileAtomically(Plugin, Descriptor.dump(2) + "\n");
	return Plugin;
}
} // namespace Mod::SdkTool
