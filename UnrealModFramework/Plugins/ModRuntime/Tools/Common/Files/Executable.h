// Copyright (c) 2026 Nelaric Contributors
/** @file Executable.h */
#pragma once

#include <filesystem>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class Executable
{
public:
	/// Returns the actual executable, independently of argv or PATH lookup.
	static fs::path ExecutablePath();
};
} // namespace Mod::BuildTool
