// Copyright (c) 2026 Nelaric Contributors
/** @file CommandDiagnostics.h */
#pragma once

#include <exception>

namespace Mod::SdkTool
{
class CommandDiagnostics
{
public:
	static int ReportCommandError(const std::exception& Error);
};
} // namespace Mod::SdkTool
