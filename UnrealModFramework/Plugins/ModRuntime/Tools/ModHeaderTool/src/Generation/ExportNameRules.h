// Copyright (c) 2026 Nelaric Contributors
/** @file ExportNameRules.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"

#include <string>
#include <vector>

namespace Mod::HeaderTool
{
class ExportNameRules
{
public:
	static std::string Fold(std::string Text);
	static void SetName(API& API, const ResolvedType& Type, const std::string& Name);
	static void AddAlias(std::vector<API>& Aliases, const API& Original, const ResolvedType& Type,
	                     const std::string& Name, const std::string& Source);
};
} // namespace Mod::HeaderTool
