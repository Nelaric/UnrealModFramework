// Copyright (c) 2026 Nelaric Contributors
/** @file EditorBuild.h */
#pragma once

#include <filesystem>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

/// Native tools and author Editor identity for an Unreal plugin build.
struct EditorBuild
{
	/// Actual engine-provided .NET host.
	fs::path Dotnet;
	/// Actual UE AutomationTool assembly.
	fs::path AutomationTool;
	/// Native MBT executable used to verify the built binary.
	fs::path Mbt;
	/// Unreal host platform for the Editor target.
	std::string Platform;
	/// Native architecture of the Editor binary.
	std::string Architecture;
	/// Build identity read from the author Editor module manifest.
	std::string BuildId;
	/// Maximum runtime for each native tool invocation.
	unsigned TimeoutSeconds = 1800;
};
} // namespace Mod::SdkTool
