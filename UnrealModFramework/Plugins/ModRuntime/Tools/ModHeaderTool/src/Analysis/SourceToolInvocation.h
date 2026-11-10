// Copyright (c) 2026 Nelaric Contributors
/** @file SourceToolInvocation.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class SourceToolInvocation
{
public:
	static fs::path ResolveSourceTool(const fs::path& Configured);
	static std::vector<std::string> SourceArguments(const GenerationModel& Model, const fs::path& Database,
	                                                const fs::path& ClangLibrary);
};
} // namespace Mod::HeaderTool
