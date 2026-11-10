// Copyright (c) 2026 Nelaric Contributors
/** @file FullView.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"

#include <set>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
struct FullView
{
	std::set<std::string> Permissions;
	std::vector<API> Members;
};
} // namespace Mod::HeaderTool
