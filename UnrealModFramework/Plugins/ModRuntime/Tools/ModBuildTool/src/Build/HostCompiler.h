// Copyright (c) 2026 Nelaric Contributors
/** @file HostCompiler.h */
#pragma once

#include "ModBuildTool/src/Build/CompiledObjects.h"
#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class HostCompiler
{
public:
	static CompiledObjects CompileObjects(const Request& Request, const fs::path& Work, const std::string& LinkWitness,
	                                      fs::file_time_type Started);
	static fs::path CreateHostArchive(const Request& Request, const fs::path& Work,
	                                  const std::vector<fs::path>& Objects, const std::string& LinkWitness);
};
} // namespace Mod::BuildTool
