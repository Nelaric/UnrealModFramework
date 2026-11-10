// Copyright (c) 2026 Nelaric Contributors
/** @file OutputPaths.h */
#pragma once

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class OutputPaths
{
public:
	static fs::path CheckedPath(const fs::path& Root, const fs::path& Relative);

	static bool SamePart(const fs::path& Left, const fs::path& Right);
};
} // namespace Mod::HeaderTool
