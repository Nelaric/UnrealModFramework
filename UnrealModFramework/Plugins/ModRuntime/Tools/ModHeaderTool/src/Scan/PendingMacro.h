// Copyright (c) 2026 Nelaric Contributors
/** @file PendingMacro.h */
#pragma once

#include "ModHeaderTool/src/Scan/Token.h"

#include <cstddef>

namespace Mod::HeaderTool
{
struct PendingMacro
{
	Detail::Token Token;
	std::size_t ArgumentsBegin = 0;
	std::size_t Depth = 0;
};
} // namespace Mod::HeaderTool
