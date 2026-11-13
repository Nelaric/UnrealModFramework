// Copyright (c) 2026 Nelaric Contributors
/** @file StageFiles.h */
#pragma once

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class StageFiles
{
public:
	static void CopyFile(const fs::path& Source, const fs::path& Destination);
	static void CopyTree(const fs::path& Source, const fs::path& Destination);
};
} // namespace Mod::SdkTool
