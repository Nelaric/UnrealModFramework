// Copyright (c) 2026 Nelaric Contributors
/** @file Binary.h */
#pragma once

#include "FileSystem.h"

namespace Mod::BuildTool
{
/// Checks a linked PE, ELF, or thin Mach-O image's kind and architecture.
void ValidateHostBinary(const fs::path& Path, std::string_view Platform, std::string_view Architecture,
                        std::string_view Kind, std::string_view LinkWitness = {});
} // namespace Mod::BuildTool
