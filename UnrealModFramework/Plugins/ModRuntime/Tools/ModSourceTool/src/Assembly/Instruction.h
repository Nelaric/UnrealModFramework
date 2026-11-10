// Copyright (c) 2026 Nelaric Contributors
/** @file Instruction.h */
#pragma once

#include <string>
#include <string_view>

namespace Mod::SourceTool
{
struct Instruction
{
	std::string File;
	unsigned Line = 0;
	std::string_view Source;
	std::string_view Text;
	std::string_view Operation;
	std::string_view Operands;
	unsigned Site = 0;
};
} // namespace Mod::SourceTool
