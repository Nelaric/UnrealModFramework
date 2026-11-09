// Copyright (c) 2026 Nelaric Contributors
/** @file FileSystem.h */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

/// Converts a filesystem path to UTF-8 with forward slashes.
std::string PathToUtf8(const fs::path& Path);

/// Converts a UTF-8 command-line or JSON path to a native path.
fs::path PathFromUtf8(std::string_view Text);

/// Reads a file without changing its byte offsets or line endings.
std::string ReadTextFile(const fs::path& Path);
} // namespace Mod::HeaderTool
