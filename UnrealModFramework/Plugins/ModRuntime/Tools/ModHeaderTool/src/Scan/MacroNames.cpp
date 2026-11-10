// Copyright (c) 2026 Nelaric Contributors
#include "MacroNames.h"

#include <string_view>

namespace Mod::HeaderTool
{
bool MacroNames::IsExportMacro(std::string_view Name)
{
	return Name == "MOD_EXPORT_API" || Name == "MOD_EXPORT_EVENT" || Name == "MOD_EXPORT_HOOK";
}
} // namespace Mod::HeaderTool
