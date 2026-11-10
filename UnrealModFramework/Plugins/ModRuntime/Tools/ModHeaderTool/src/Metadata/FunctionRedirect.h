// Copyright (c) 2026 Nelaric Contributors
/** @file FunctionRedirect.h */
#pragma once

#include "ModHeaderTool/src/Metadata/RedirectFunctionName.h"

#include <string>

namespace Mod::HeaderTool
{
/// An effective UE FunctionRedirects entry and its location in configuration.
struct FunctionRedirect
{
	/// Previous name.
	RedirectFunctionName OldName;
	/// Replacement name with qualifiers inherited from the previous name.
	RedirectFunctionName NewName;
	/// Source INI location used in diagnostics.
	std::string Location;
	/// Effective configuration value.
	std::string Entry;
	/// Whether the redirect uses substring or wildcard matching.
	bool bPattern = false;
	/// Whether the entry explicitly names a replacement.
	bool bHasNewName = false;
};
} // namespace Mod::HeaderTool
