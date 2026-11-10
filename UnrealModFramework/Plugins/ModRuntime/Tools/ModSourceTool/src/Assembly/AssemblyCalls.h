// Copyright (c) 2026 Nelaric Contributors
/** @file AssemblyCalls.h */
#pragma once

#include "ModSourceTool/src/Assembly/Instruction.h"

#include <nlohmann/json.hpp>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class AssemblyCalls
{
public:
	static std::string_view CalledSymbol(const Instruction& Instruction);
	static void AnalyzeCallPath(Json& Failure, const std::vector<Instruction>& Body);

private:
	static bool At(const Instruction& Instruction, const Json& Location);
};
} // namespace Mod::SourceTool
