// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationOutput.h */
#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class GenerationOutput
{
public:
	/// Replaces fixed generated paths and commits their complete manifest last.
	static void PublishGeneratedFiles(const fs::path& Root, const std::map<std::string, std::string>& Files,
	                                  const std::string& ManifestName);
};
} // namespace Mod::HeaderTool
