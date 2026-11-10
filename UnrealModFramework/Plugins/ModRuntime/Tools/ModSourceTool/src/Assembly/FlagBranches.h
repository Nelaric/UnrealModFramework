// Copyright (c) 2026 Nelaric Contributors
/** @file FlagBranches.h */
#pragma once

#include <cstddef>

namespace Mod::SourceTool
{
struct FlagBranches
{
	std::size_t NullTest;
	std::size_t NullBranch;
	std::size_t FlagLoad;
	std::size_t FlagTest;
	std::size_t FlagBranch;
};
} // namespace Mod::SourceTool
