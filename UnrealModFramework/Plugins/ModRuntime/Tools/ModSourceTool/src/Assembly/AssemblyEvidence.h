// Copyright (c) 2026 Nelaric Contributors
/** @file AssemblyEvidence.h */
#pragma once

#include "ModSourceTool/src/Assembly/DebugLocations.h"
#include "ModSourceTool/src/Assembly/Instruction.h"

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class AssemblyEvidence
{
public:
	static void SetEvidence(Json& Failure, const Json& Evidence);
	static void AnalyzeGasValidity(Json& Failure, const std::vector<Instruction>& Body, const DebugLocations& Locations,
	                               std::uint64_t Flag);
	static void AnalyzeValidity(Json& Failure, const std::vector<Instruction>& Body, const std::string& File,
	                            std::uint64_t Flag, const std::map<unsigned, unsigned>& CallsByLine);
};
} // namespace Mod::SourceTool
