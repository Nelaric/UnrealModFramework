// Copyright (c) 2026 Nelaric Contributors
/** @file HostPublisher.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <filesystem>
#include <map>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class HostPublisher
{
public:
	static void PublishHostArchive(const Request& Request, const fs::path& Archive, const std::string& LinkWitness,
	                               const std::map<fs::path, std::string>& Dependencies);
};
} // namespace Mod::BuildTool
