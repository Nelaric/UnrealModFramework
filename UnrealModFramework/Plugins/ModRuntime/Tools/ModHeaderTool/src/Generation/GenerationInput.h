// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationInput.h */
#pragma once

#include "ModHeaderTool/src/Generation/ModuleInput.h"
#include "ModHeaderTool/src/Project/Project.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct ReflectedType
{
	std::string Module;
	std::string Path;
	std::string LegacyId;
};

struct GenerationInput
{
	Mod::HeaderTool::Project Project;
	fs::path File;
	fs::path Database;
	std::string GameId;
	std::string Target;
	std::string Platform;
	std::string Configuration;
	std::string Architecture;
	bool bAutoDiscoverModules = false;
	std::vector<ModuleInput> Modules;
	std::map<std::pair<std::string, fs::path>, ReflectedType> Reflection;
	std::vector<std::string> AnalysisArguments;
	std::map<std::string, fs::path> PathRoots;
};
} // namespace Mod::HeaderTool
