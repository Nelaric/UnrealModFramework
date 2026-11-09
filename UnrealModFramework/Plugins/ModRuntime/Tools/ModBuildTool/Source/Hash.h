// Copyright (c) 2026 Nelaric Contributors
/** @file Hash.h */
#pragma once

#include "FileSystem.h"

#include <string>
#include <string_view>

namespace Mod::BuildTool
{
/// Returns the lowercase SHA-256 digest of exact bytes.
std::string HashBytes(std::string_view Bytes);
/// Returns the SHA-256 digest of a regular file without text conversion.
std::string HashFile(const fs::path& Path);
/// Requires a canonical lowercase SHA-256 digest.
void RequireDigest(std::string_view Digest, std::string_view Field);
} // namespace Mod::BuildTool
