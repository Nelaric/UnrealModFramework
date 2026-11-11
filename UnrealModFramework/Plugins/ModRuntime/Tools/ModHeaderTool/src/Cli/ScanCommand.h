// Copyright (c) 2026 Nelaric Contributors
/** @file ScanCommand.h */
#pragma once

#include <filesystem>

namespace Mod::HeaderTool
{
class ScanCommand
{
public:
	static int RunScan(const std::filesystem::path& Project, const std::filesystem::path& Snapshot);
};
} // namespace Mod::HeaderTool
