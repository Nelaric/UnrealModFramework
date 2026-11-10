// Copyright (c) 2026 Nelaric Contributors
/** @file Location.h */
#pragma once

#include <clang-c/Index.h>

namespace Mod::SourceTool
{
struct Location
{
	CXFile File = nullptr;
	unsigned Line = 0;
	unsigned Column = 0;
};
} // namespace Mod::SourceTool
