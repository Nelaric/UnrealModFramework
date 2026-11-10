// Copyright (c) 2026 Nelaric Contributors
#include "EditorBuildOptions.h"

#include "ModSDKTool/src/Cli/CommandOptions.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Options = std::map<std::string, std::string>;

EditorBuild EditorBuildOptions::ReadEditorBuild(const Options& Options)
{
	const std::string& Architecture = Options.at("--architecture");
	if (Architecture != "x64" && Architecture != "arm64")
	{
		throw std::runtime_error("--architecture must be x64 or arm64.");
	}
	return {
	    .Dotnet = fs::canonical(CommandOptions::OptionPath(Options, "--dotnet")),
	    .AutomationTool = fs::canonical(CommandOptions::OptionPath(Options, "--automation-tool")),
	    .Mbt = fs::canonical(CommandOptions::OptionPath(Options, "--mbt")),
#ifdef _WIN32
	    .Platform = "Win64",
#elif defined(__APPLE__)
	    .Platform = "Mac",
#else
	    .Platform = "Linux",
#endif
	    .Architecture = Architecture,
	    .BuildId = JsonDocument::Text(JsonDocument::ReadJson(CommandOptions::OptionPath(Options, "--editor-modules")),
	                                  "BuildId"),
	};
}
} // namespace Mod::SdkTool
