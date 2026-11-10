// Copyright (c) 2026 Nelaric Contributors
/** @file DebugLocations.h */
#pragma once

#include "ModSourceTool/src/Assembly/InlineSite.h"

#include <map>
#include <string>

namespace Mod::SourceTool
{
struct DebugLocations
{
	std::map<unsigned, std::string> Files;
	std::map<unsigned, InlineSite> Sites;

	std::string File(unsigned Id) const;
};
} // namespace Mod::SourceTool
