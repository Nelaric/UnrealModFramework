// Copyright (c) 2026 Nelaric Contributors
/** @file OptionsCheck.h */
#pragma once

#include <filesystem>

namespace Mod::HeaderTool
{
class OptionsCheck
{
public:
	static int CheckOptions(const std::filesystem::path& ProjectFile, const std::filesystem::path& Snapshot);
};
} // namespace Mod::HeaderTool
