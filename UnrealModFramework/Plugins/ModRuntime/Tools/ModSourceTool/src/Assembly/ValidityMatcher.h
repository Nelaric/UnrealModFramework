// Copyright (c) 2026 Nelaric Contributors
/** @file ValidityMatcher.h */
#pragma once

#include "ModSourceTool/src/Assembly/DebugLocations.h"
#include "ModSourceTool/src/Assembly/FlagBranches.h"
#include "ModSourceTool/src/Assembly/Instruction.h"
#include "ModSourceTool/src/Assembly/ValidityBranches.h"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class ValidityMatcher
{
public:
	static std::optional<ValidityBranches> MatchValidity(const std::vector<Instruction>& Body, std::size_t First,
	                                                     std::string_view Header, std::uint64_t Flag);
	static bool InlineSiteMatches(unsigned SiteId, const DebugLocations& Locations, const Json& Call,
	                              const Json& Condition);
	static std::optional<FlagBranches> MatchGasValidity(const std::vector<Instruction>& Body, std::size_t Load,
	                                                    std::uint64_t Flag);

private:
	static std::string_view ObjectRegister(std::string_view Address);
};
} // namespace Mod::SourceTool
