// Copyright (c) 2026 Nelaric Contributors
/** @file UeHookBuild.h */
#pragma once

#include <filesystem>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

struct UeHookTarget
{
	fs::path Engine;
	fs::path Project;
	std::string Target;
	std::string Platform;
	std::string Configuration;
	std::string Architecture;
	std::string Mode;
	fs::path Metadata;
	std::string SourceCommit;
	std::string EngineCommit;
};

class UeHookBuild
{
public:
	/// Builds one UBT target using the current MHT-generated Hook sources.
	static void Run(const UeHookTarget& Target);
	static fs::path MetadataInputs(const UeHookTarget& Target);
	static void ExportGameActions(const UeHookTarget& Target, const fs::path& Actions, bool bPrepare);
	static void ExecuteGameActions(const UeHookTarget& Target, const fs::path& Actions);
};
} // namespace Mod::BuildTool
