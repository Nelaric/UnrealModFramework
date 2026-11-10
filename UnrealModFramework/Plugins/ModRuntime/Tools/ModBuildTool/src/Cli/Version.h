// Copyright (c) 2026 Nelaric Contributors
/** @file Version.h */
#pragma once

#include <string_view>

namespace Mod::BuildTool
{
class Version
{
public:
	/// Version of the command and artifact verification implementation.
	static constexpr std::string_view ToolVersion = "0.3.0";
};
} // namespace Mod::BuildTool
