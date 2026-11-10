// Copyright (c) 2026 Nelaric Contributors
#include "EditorBuildCommand.h"

#include "ModSDKTool/src/Cli/CommandOptions.h"
#include "ModSDKTool/src/Cli/EditorBuildOptions.h"
#include "ModSDKTool/src/Editor/EditorPlugin.h"

#include <map>
#include <string>

namespace Mod::SdkTool
{
using Options = std::map<std::string, std::string>;

int EditorBuildCommand::BuildEditorCommand(const Options& Options)
{
	const auto Editor = EditorBuildOptions::ReadEditorBuild(Options);
	EditorPlugin::BuildEditorPlugin(Editor, CommandOptions::OptionPath(Options, "--plugin"),
	                                CommandOptions::OptionPath(Options, "--package"));
	return 0;
}
} // namespace Mod::SdkTool
