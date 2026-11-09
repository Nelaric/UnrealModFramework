// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationOutput.h */
#pragma once

#include "ModHeaderTool/FileSystem.h"

#include <map>
#include <string>

namespace Mod::HeaderTool
{
/// Replaces fixed generated paths and commits their complete manifest last.
void PublishGeneratedFiles(const fs::path& Root, const std::map<std::string, std::string>& Files,
                           const std::string& ManifestName);
} // namespace Mod::HeaderTool
