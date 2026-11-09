// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataValidator.h */
#pragma once

#include "ModHeaderTool/MacroScanner.h"
#include "ModHeaderTool/MetadataOptions.h"

namespace Mod::HeaderTool
{
/// Validates parsed fields, adds defaults, and returns located diagnostics.
std::vector<std::string> ValidateMetadata(FExportMacro& Macro, const FMetadataOptions* Options,
                                          const std::string& Source);
} // namespace Mod::HeaderTool
