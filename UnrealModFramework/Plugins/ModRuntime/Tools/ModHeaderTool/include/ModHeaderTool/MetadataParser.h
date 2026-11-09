// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataParser.h */
#pragma once

#include "ModHeaderTool/MacroScanner.h"

namespace Mod::HeaderTool
{
/// Parses named literal arguments and reports physical source locations.
std::vector<FMetadataField> ParseMetadata(const FExportMacro& Macro, const std::string& Source);
} // namespace Mod::HeaderTool
