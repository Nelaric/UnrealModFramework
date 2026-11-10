// Copyright (c) 2026 Nelaric Contributors
/** @file TemporaryName.h */
#pragma once

#include <string>

namespace Mod::BuildTool
{
class TemporaryName
{
public:
	/// Returns a process-local unique suffix for owned temporary paths.
	static std::string UniqueSuffix();
};
} // namespace Mod::BuildTool
