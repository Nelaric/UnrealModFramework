// Copyright (c) 2026 Nelaric Contributors
/** @file AssemblyListing.h */
#pragma once

#include "ModSourceTool/src/Assembly/DebugLocations.h"
#include "ModSourceTool/src/Assembly/Instruction.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

class AssemblyListing
{
public:
	static DebugLocations ReadDebugLocations(std::string_view Listing, const fs::path& Directory);
	static std::vector<Instruction> ReadGasProcedure(std::string_view Listing, std::string_view Symbol,
	                                                 const std::string& SourceFile, const DebugLocations& Locations);
	static std::vector<Instruction> ReadProcedure(std::string_view Listing, std::string_view Symbol,
	                                              const fs::path& Directory, const std::string& SourceFile);
};
} // namespace Mod::SourceTool
