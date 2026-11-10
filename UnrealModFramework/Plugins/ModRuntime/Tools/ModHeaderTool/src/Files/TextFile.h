// Copyright (c) 2026 Nelaric Contributors
/** @file TextFile.h */
#pragma once

#include <filesystem>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class TextFile
{
public:
	/// Reads a file without changing its byte offsets or line endings.
	static std::string ReadTextFile(const fs::path& Path);
};
} // namespace Mod::HeaderTool
