// Copyright (c) 2026 Nelaric Contributors
/** @file BuildProgress.h */
#pragma once

#include <string_view>

namespace Mod::SdkTool
{
class BuildProgress
{
public:
	/// Emits one machine-readable progress event to stderr.
	static void ReportStage(std::string_view Stage, std::string_view Message);
};
} // namespace Mod::SdkTool
