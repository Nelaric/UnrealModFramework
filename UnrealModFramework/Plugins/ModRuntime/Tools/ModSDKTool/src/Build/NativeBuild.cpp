// Copyright (c) 2026 Nelaric Contributors
#include "NativeBuild.h"

#include "Common/Files/Path.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Manifest/Library.h"
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

Json NativeBuild::BuildNativeLibraries(const SdkRequest& Request, const fs::path& Work)
{
	Json LibraryResults = Json::array();
	BuildProgress::ReportStage("BuildLibraries", "Building the declared generated native libraries with MBT.");
	for (std::size_t Index = 0; Index < Request.Libraries.size(); ++Index)
	{
		const Library& Job = Request.Libraries[Index];
		const fs::path Result = Work / ("library-" + std::to_string(Index) + ".json");
		const int Code = ToolProcess::RunTool(
		    Request.Mbt,
		    {"build-host", "--context", Mod::BuildTool::Path::PathToUtf8(Job.Context), "--generated-manifest",
		     Mod::BuildTool::Path::PathToUtf8(Job.Manifest), "--expected-input",
		     JsonDocument::Text(Job.Plan, "inputFingerprint"), "--output", Mod::BuildTool::Path::PathToUtf8(Result)},
		    Request.Project.parent_path(), Request.Document.value("timeoutSeconds", 1800u));
		if (Code != 0)
		{
			throw std::runtime_error("MBT library build failed: " + Mod::BuildTool::Path::PathToUtf8(Job.Context));
		}
		LibraryResults.push_back({{"role", Job.Role}, {"result", JsonDocument::ReadJson(Result)}});
	}
	return LibraryResults;
}
} // namespace Mod::SdkTool
