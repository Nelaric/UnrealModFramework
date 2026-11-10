// Copyright (c) 2026 Nelaric Contributors
/** @file RedirectFunctionName.h */
#pragma once

#include <string>

namespace Mod::HeaderTool
{
/// The UE-parsed parts of a Core Redirect function path.
struct RedirectFunctionName
{
	/// Script package, empty for an unqualified name.
	std::string Package;
	/// Reflected class name, empty for an unqualified name.
	std::string Class;
	/// Reflected function name.
	std::string Function;
};
} // namespace Mod::HeaderTool
