// Copyright (c) 2026 Nelaric Contributors
/** @file Archive.h */
#pragma once

#include "FileSystem.h"

#include <vector>

namespace Mod::BuildTool
{
/// Requires native objects and every requested symbol in the archive index.
void ValidateHostArchive(const fs::path& Path, std::string_view Platform, std::string_view Architecture,
                         const std::vector<std::string>& RequiredSymbols);
} // namespace Mod::BuildTool
