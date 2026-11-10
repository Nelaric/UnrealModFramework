// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataName.h */
#pragma once

#include <string_view>

namespace Mod::HeaderTool
{
class MetadataName
{
public:
	/// Checks whether a name can be emitted as a compatible C++/UE function.
	static bool IsCompatibleFunctionName(std::string_view Name);
};
} // namespace Mod::HeaderTool
