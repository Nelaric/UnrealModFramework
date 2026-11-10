// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataValidator.h */
#pragma once

#include "ModHeaderTool/src/Metadata/MetadataOptions.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <string>
#include <vector>

namespace Mod::HeaderTool
{
class MetadataValidator
{
public:
	/// Validates parsed fields, adds defaults, and returns located diagnostics.
	static std::vector<std::string> ValidateMetadata(ExportMacro& Macro, const MetadataOptions* Options,
	                                                 const std::string& Source);
};
} // namespace Mod::HeaderTool
