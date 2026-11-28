// Copyright (c) 2026 Nelaric Contributors
#include "EditorPackage.h"
#include "Common/Files/File.h"

#include "Common/Files/Executable.h"
#include "Common/Files/Path.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Build/StageFiles.h"
#include "ModSDKTool/src/Editor/PluginSources.h"
#include "ModSDKTool/src/Manifest/JsonDocument.h"
#include "ModSDKTool/src/Process/ToolProcess.h"

#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

fs::path EditorPackage::FindUnrealBuildTool(const fs::path& AutomationTool)
{
	const fs::path Ubt = AutomationTool.parent_path().parent_path() / "UnrealBuildTool" / "UnrealBuildTool.dll";
	if (!fs::is_regular_file(Ubt))
	{
		throw std::runtime_error("Missing UnrealBuildTool next to the supplied AutomationTool assembly.");
	}
	return Ubt;
}

void EditorPackage::BuildPlugin(const EditorBuild& Editor, const fs::path& Plugin, const fs::path& Package)
{
	using Mod::BuildTool::File;
	using Mod::BuildTool::Path;
	const fs::path Ubt = FindUnrealBuildTool(Editor.AutomationTool);
	fs::path Host = Package;
	Host += "h";
	const fs::path Project = Host / "AuthoringSdk.uproject";
	const fs::path BuiltPlugin = Host / "Plugins" / Plugin.stem();
	StageFiles::CopyTree(Plugin.parent_path(), BuiltPlugin);
	const Json ProjectDescriptor = {
	    {"FileVersion", 3},
	    {"DisableEnginePluginsByDefault", true},
	    {"Modules", Json::array({{{"Name", "AuthoringSdk"}, {"Type", "Runtime"}, {"LoadingPhase", "Default"}}})},
	    {"Plugins", Json::array({{{"Name", Path::PathToUtf8(Plugin.stem())}, {"Enabled", true}}})}};
	File::WriteFileAtomically(Project, ProjectDescriptor.dump(2) + "\n");
	// The engine's UnrealEditor target enumerates every engine module. Use a project Editor target for this SDK.
	File::WriteFileAtomically(Host / "Source" / "AuthoringSdkEditor.Target.cs",
	                          "// Copyright (c) 2026 Nelaric Contributors\nusing UnrealBuildTool;\n"
	                          "public class AuthoringSdkEditorTarget : TargetRules\n"
	                          "{\n"
	                          "    public AuthoringSdkEditorTarget(TargetInfo Target) : base(Target)\n"
	                          "    {\n"
	                          "        Type = TargetType.Editor;\n"
	                          "        DefaultBuildSettings = BuildSettingsVersion.V5;\n"
	                          "        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_6;\n"
	                          "        ExtraModuleNames.Add(\"AuthoringSdk\");\n"
	                          "    }\n"
	                          "}\n");
	File::WriteFileAtomically(Host / "Source" / "AuthoringSdk" / "AuthoringSdk.Build.cs",
	                          "// Copyright (c) 2026 Nelaric Contributors\nusing UnrealBuildTool;\n"
	                          "public class AuthoringSdk : ModuleRules\n"
	                          "{\n"
	                          "    public AuthoringSdk(ReadOnlyTargetRules Target) : base(Target)\n"
	                          "    {\n"
	                          "        PrivateDependencyModuleNames.Add(\"Core\");\n"
	                          "    }\n"
	                          "}\n");
	File::WriteFileAtomically(
	    Host / "Source" / "AuthoringSdk" / "AuthoringSdk.cpp",
	    "// Copyright (c) 2026 Nelaric Contributors\n#include \"Modules/ModuleManager.h\"\n"
	    "IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, AuthoringSdk, \"AuthoringSdk\");\n");

	const int Code = ToolProcess::RunTool(
	    Editor.Dotnet,
	    {Path::PathToUtf8(Ubt), "AuthoringSdkEditor", Editor.Platform, "Development",
	     "-Project=" + Path::PathToUtf8(Project), "-Plugin=" + Path::PathToUtf8(BuiltPlugin / Plugin.filename()),
	     "-Architecture=" + Editor.Architecture, "-NoEngineChanges", "-NoHotReload", "-WaitMutex", "-NoUBTMakefiles",
	     "-NoUBA", "-NoPCH", "-NoSharedPCH", "-DisableUnity", "-Log=" + Path::PathToUtf8(Host / "Build.log")},
	    Host, Editor.TimeoutSeconds);
	if (Code != 0)
	{
		throw std::runtime_error("SDK Editor build failed with exit code " + std::to_string(Code) + ": " +
		                         Path::PathToUtf8(Plugin));
	}

	StageFiles::CopyTree(Plugin.parent_path(), Package);
	StageFiles::CopyTree(BuiltPlugin / "Binaries", Package / "Binaries");
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(BuiltPlugin / "Intermediate"))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("Unexpected symbolic link in SDK Editor build products.");
		}
		const fs::path Relative = Entry.path().lexically_relative(BuiltPlugin);
		const auto Extension = Relative.extension();
		if (Entry.is_regular_file() && (std::find(Relative.begin(), Relative.end(), "Inc") != Relative.end() ||
		                                Extension == ".lib" || Extension == ".a" || Extension == ".precompiled"))
		{
			StageFiles::CopyFile(Entry.path(), Package / Relative);
		}
	}
	const fs::path Engine = Ubt.parent_path().parent_path().parent_path().parent_path();
	const Json Version = JsonDocument::ReadJson(Engine / "Build" / "Build.version");
	const fs::path DescriptorPath = Package / Plugin.filename();
	Json Descriptor = JsonDocument::ReadJson(DescriptorPath);
	Descriptor.erase("EnabledByDefault");
	Descriptor["Installed"] = true;
	Descriptor["EngineVersion"] = std::to_string(Version.at("MajorVersion").get<int>()) + "." +
	                              std::to_string(Version.at("MinorVersion").get<int>()) + ".0";
	File::WriteFileAtomically(DescriptorPath, Descriptor.dump(2) + "\n");
}

void EditorPackage::VerifyEditorModule(const EditorBuild& Editor, const fs::path& Package, const std::string& Module)
{
	const fs::path Binaries = Package / "Binaries";
	if (!fs::is_directory(Binaries))
	{
		throw std::runtime_error("The SDK Editor build did not produce binaries.");
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
	const fs::path SdkPackage = Work / "s";
	const fs::path DevelopmentPackage = Work / "d";

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
