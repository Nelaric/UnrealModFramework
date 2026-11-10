// Copyright (c) 2026 Nelaric Contributors
/** @file MacroNames.h */
#pragma once

#include <string_view>

namespace Mod::HeaderTool
{
class MacroNames
{
public:
	static bool IsExportMacro(std::string_view Name);
};
} // namespace Mod::HeaderTool
