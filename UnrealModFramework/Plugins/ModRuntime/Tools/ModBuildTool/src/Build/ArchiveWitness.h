// Copyright (c) 2026 Nelaric Contributors
/** @file ArchiveWitness.h */
#pragma once

#include "ModBuildTool/src/Build/Request.h"

#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
class ArchiveWitness
{
public:
	static constexpr std::string_view LinkWitnessPrefix = "UMF_ModHostLink_";
	static std::vector<std::string> ArchiveSymbols(const Request& Request, const std::string& Witness);
};
} // namespace Mod::BuildTool
