// Copyright (c) 2026 Nelaric Contributors
/** @file AssemblyFlow.h */
#pragma once

#include "ModSourceTool/src/Assembly/Instruction.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
class AssemblyFlow
{
public:
	static std::string_view BranchLabel(const Instruction& Branch);
	static bool ReturnsFalse(const std::vector<Instruction>& Body, std::string_view Label, bool bZero = false);
	static bool BelongsToGuard(const std::vector<Instruction>& Body, std::size_t First, std::size_t Last,
	                           const std::string& File, unsigned Line);
	static bool ZeroBeforeBranch(const std::vector<Instruction>& Body, std::size_t Branch);

private:
	static bool IsReturnRegister(std::string_view Register);
	static bool IsTrapBlock(const std::vector<Instruction>& Body, std::string_view Label);
};
} // namespace Mod::SourceTool
