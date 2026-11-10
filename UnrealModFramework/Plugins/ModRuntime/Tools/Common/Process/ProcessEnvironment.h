// Copyright (c) 2026 Nelaric Contributors
/** @file ProcessEnvironment.h */
#pragma once

#include <map>
#include <string>

namespace Mod::BuildTool
{
class ProcessEnvironment
{
public:
	/// Captures the child environment before applying explicit context values.
	static std::map<std::string, std::string> CaptureEnvironment();
};
} // namespace Mod::BuildTool
