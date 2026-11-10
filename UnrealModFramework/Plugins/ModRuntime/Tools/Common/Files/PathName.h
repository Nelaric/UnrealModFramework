// Copyright (c) 2026 Nelaric Contributors
/** @file PathName.h */
#pragma once

#include <filesystem>
#include <string>

namespace Mod::Tools
{
namespace fs = std::filesystem;

class PathName
{
public:
	static std::u8string LowercaseName(const fs::path& Path);
};
} // namespace Mod::Tools
