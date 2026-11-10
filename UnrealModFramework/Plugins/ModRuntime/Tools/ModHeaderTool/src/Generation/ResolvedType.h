// Copyright (c) 2026 Nelaric Contributors
/** @file ResolvedType.h */
#pragma once

#include "ModHeaderTool/src/Generation/TypeIdentity.h"

#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
struct ResolvedType
{
	TypeIdentity Identity;
	std::string Module;
	std::string HeaderInclude;
	std::vector<std::size_t> APIs;
	std::set<std::string> MethodNames;
	std::string Symbol;
	std::string Signature;
	bool bLocalPlayerSubsystem = false;
	bool bFullView = false;
	bool bAllowWorldless = false;
};
} // namespace Mod::HeaderTool
