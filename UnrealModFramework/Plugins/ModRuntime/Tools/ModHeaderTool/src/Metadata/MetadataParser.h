// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataParser.h */
#pragma once

#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"

#include <string>
#include <vector>

namespace Mod::HeaderTool
{
class MetadataParser
{
public:
	/// Parses named literal arguments and reports physical source locations.
	static std::vector<MetadataField> ParseMetadata(const ExportMacro& Macro, const std::string& Source);
};
} // namespace Mod::HeaderTool
