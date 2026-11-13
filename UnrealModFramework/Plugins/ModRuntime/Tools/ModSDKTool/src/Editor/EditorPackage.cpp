// Copyright (c) 2026 Nelaric Contributors
#include "EditorPackage.h"

#include "Common/Files/Executable.h"
#include "Common/Files/Path.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Build/StageFiles.h"
#include "ModSDKTool/src/Editor/PluginSources.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Process/ToolProcess.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void EditorPackage::BuildPlugin(const EditorBuild& Editor, const fs::path& Plugin, const fs::path& Package)
{
	const int Code = ToolProcess::RunTool(
	    Editor.Dotnet,
	    {Mod::BuildTool::Path::PathToUtf8(Editor.AutomationTool), "BuildPlugin",
	     "-Plugin=" + Mod::BuildTool::Path::PathToUtf8(Plugin), "-Package=" + Mod::BuildTool::Path::PathToUtf8(Package),
	     "-HostPlatforms=" + Editor.Platform, "-Architecture_" + Editor.Platform + "=" + Editor.Architecture,
	     "-NoTargetPlatforms", "-StrictIncludes", "-Unattended", "-UTF8Output"},
	    Editor.AutomationTool.parent_path(), Editor.TimeoutSeconds);
	if (Code != 0)
	{
		throw std::runtime_error("UE BuildPlugin failed with exit code " + std::to_string(Code) + ": " +
		                         Mod::BuildTool::Path::PathToUtf8(Plugin));
	}
}

void EditorPackage::VerifyEditorModule(const EditorBuild& Editor, const fs::path& Package, const std::string& Module)
{
	const fs::path Binaries = Package / "Binaries";
	if (!fs::is_directory(Binaries))
	{
		throw std::runtime_error("BuildPlugin did not produce Editor binaries.");
	}
	bool bFound = false;
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Binaries))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("Unexpected symbolic link in UE binaries.");
		}
		if (!Entry.is_regular_file() || Entry.path().extension() != ".modules")
		{
			continue;
		}
		const Json Modules = JsonDocument::ReadJson(Entry.path());
		if (!Modules.at("Modules").contains(Module))
		{
			continue;
		}
		if (bFound || Modules.at("BuildId") != Editor.BuildId)
		{
			throw std::runtime_error("Duplicate or incompatible UE Editor module: " + Module);
		}
		const std::string BinaryName = Modules.at("Modules").at(Module).get<std::string>();
		if (Mod::BuildTool::Path::PathFromUtf8(BinaryName).filename() != Mod::BuildTool::Path::PathFromUtf8(BinaryName))
		{
			throw std::runtime_error("UE module metadata contains a non-local binary name.");
		}
		const fs::path Binary = Entry.path().parent_path() / Mod::BuildTool::Path::PathFromUtf8(BinaryName);
		const int Code =
		    ToolProcess::RunTool(Editor.Mbt,
		                         {"inspect-binary", "--file", Mod::BuildTool::Path::PathToUtf8(Binary), "--platform",
		                          Editor.Platform, "--architecture", Editor.Architecture, "--kind", "DynamicLibrary"},
		                         Package, Editor.TimeoutSeconds);
		if (Code != 0)
		{
			throw std::runtime_error("MBT rejected the built Editor binary: " + Module);
		}
		bFound = true;
	}
	if (!bFound)
	{
		throw std::runtime_error("Missing successful UE Editor module metadata: " + Module);
	}
}

void EditorPackage::BuildDevelopmentPlugin(const EditorBuild& Editor, const fs::path& Work, const fs::path& Package)
{
	BuildProgress::ReportStage("BuildDevelopmentPlugin", "Generating and building the SDK development editor plugin.");
	BuildPlugin(Editor, PluginSources::PrepareDevelopmentSource(Work), Package);
	VerifyEditorModule(Editor, Package, PluginSources::DevelopmentModuleName);
	const fs::path Tool = Mod::BuildTool::Executable::ExecutablePath();
	const fs::path ToolDirectory = Package / "Binaries" / "ThirdParty" / "ModSDKTool" / Editor.Platform;
	StageFiles::CopyFile(Tool, ToolDirectory / Tool.filename());
	StageFiles::CopyFile(Editor.Mbt, ToolDirectory / Editor.Mbt.filename());
	for (const char* Name : {"UnrealModFramework.LICENSE", "nlohmann.LICENSE.MIT", "NOTICE.md"})
	{
		StageFiles::CopyFile(Tool.parent_path() / "Licenses" / Name, ToolDirectory / "Licenses" / Name);
	}
}

PluginPackages EditorPackage::BuildSdkPackages(const SdkRequest& Request, const fs::path& Work,
                                               const std::string& Plugin)
{
	const fs::path Source = Work / "SdkSource";
	const fs::path SdkPackage = Work / "SdkPackage";
	const fs::path DevelopmentPackage = Work / "DevelopmentPackage";

	PluginSources::PrepareSdkSource(Request, Source);
	const Json& EditorDocument = Request.Document.at("editor");
	const EditorBuild Editor{Request.Dotnet,
	                         Request.AutomationTool,
	                         Request.Mbt,
	                         JsonDocument::Text(EditorDocument, "platform"),
	                         JsonDocument::Text(EditorDocument, "architecture"),
	                         JsonDocument::Text(EditorDocument, "buildId"),
	                         Request.Document.value("timeoutSeconds", 1800u)};
	BuildProgress::ReportStage("BuildSdkPlugin", "Building an author-Editor SDK with normal UHT and UBT.");
	BuildPlugin(Editor, Source / (Plugin + ".uplugin"), SdkPackage);
	VerifyEditorModule(Editor, SdkPackage, Plugin + "Runtime");
	if (!Request.LifecycleArtifact.is_null())
	{
		VerifyEditorModule(Editor, SdkPackage, "ModLoader");
	}
	BuildDevelopmentPlugin(Editor, Work, DevelopmentPackage);
	return {SdkPackage, DevelopmentPackage};
}
} // namespace Mod::SdkTool
