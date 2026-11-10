// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataValidator.h */
#pragma once

#include "ModHeaderTool/MacroScanner.h"
#include "ModHeaderTool/MetadataOptions.h"

#include <string_view>

namespace Mod::HeaderTool
{
/// Checks whether a name can be emitted as a compatible C++/UE function.
bool IsCompatibleFunctionName(std::string_view Name);

/// Validates parsed fields, adds defaults, and returns located diagnostics.
std::vector<std::string> ValidateMetadata(FExportMacro& Macro, const FMetadataOptions* Options,
                                          const std::string& Source);
} // namespace Mod::HeaderTool
