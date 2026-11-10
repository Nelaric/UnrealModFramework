// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationInput.h */
#pragma once

#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Project/Project.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct GenerationInput
{
	Mod::HeaderTool::Project Project;
	fs::path File;
	fs::path Database;
	fs::path IdentityFile;
	std::string GameId;
	std::string Target;
	std::string Platform;
	std::string Configuration;
	std::string Architecture;
	std::vector<ModuleInput> Modules;
	std::map<std::string, TypeIdentity> Types;
	std::vector<std::string> AnalysisArguments;
	std::map<std::string, fs::path> PathRoots;
	std::map<std::string, fs::path> HostContexts;
};
} // namespace Mod::HeaderTool
