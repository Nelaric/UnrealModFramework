// Copyright (c) 2026 Nelaric Contributors
/** @file Guard.h */
#pragma once

#include <clang-c/Index.h>

namespace Mod::SourceTool
{
struct Guard
{
	CXCursor Condition;
	bool bTrue;
};
} // namespace Mod::SourceTool
