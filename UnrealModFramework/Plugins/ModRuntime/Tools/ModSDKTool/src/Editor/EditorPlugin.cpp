// Copyright (c) 2026 Nelaric Contributors
#include "EditorPlugin.h"

#include "Common/Files/BuildLock.h"
#include "Common/Files/Path.h"
#include "ModSDKTool/src/Build/BuildProgress.h"
#include "ModSDKTool/src/Build/StageFiles.h"
#include "ModSDKTool/src/Build/Workspace.h"
#include "ModSDKTool/src/Editor/EditorPackage.h"

#include <filesystem>
#include <stdexcept>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Mod::BuildTool::BuildLock;

void EditorPlugin::BuildEditorPlugin(const EditorBuild& Editor, const fs::path& Package)
{
	fs::path Output = fs::weakly_canonical(fs::absolute(Package));
	if (Output.has_relative_path() && !Output.has_filename())
	{
		Output = Output.parent_path();
	}
	BuildLock Lock(Output, ".mst-lock");
	if (fs::exists(Output))
	{
		throw std::runtime_error("The package directory already exists: " + Mod::BuildTool::Path::PathToUtf8(Output));
	}

	const Workspace Work(fs::temp_directory_path());
	const fs::path StagedPackage = Work.Root / "Package";
	EditorPackage::BuildDevelopmentPlugin(Editor, Work.Root, StagedPackage);

	// Copy onto the destination filesystem before committing the package.
	const Workspace Incoming(Output.parent_path());
	const fs::path IncomingPackage = Incoming.Root / "Package";
	StageFiles::CopyTree(StagedPackage, IncomingPackage);
	if (fs::exists(Output))
	{
		throw std::runtime_error("The package directory already exists: " + Mod::BuildTool::Path::PathToUtf8(Output));
	}
	fs::rename(IncomingPackage, Output);
	BuildProgress::ReportStage("Publish",
	                           "Published verified authoring plugin at " + Mod::BuildTool::Path::PathToUtf8(Output));
}
} // namespace Mod::SdkTool
