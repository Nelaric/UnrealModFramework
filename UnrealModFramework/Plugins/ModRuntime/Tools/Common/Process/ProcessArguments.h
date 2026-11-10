// Copyright (c) 2026 Nelaric Contributors
/** @file ProcessArguments.h */
#pragma once

#include <string>
#include <string_view>

namespace Mod::BuildTool
{
class ProcessArguments
{
public:
	static std::string QuoteMsvc(std::string_view Value);
	static void RequireArgument(std::string_view Value);
};
} // namespace Mod::BuildTool
