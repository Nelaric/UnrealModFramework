// Copyright (c) 2026 Nelaric Contributors
#include "MetadataParser.h"

#include "ModHeaderTool/src/Metadata/Parser.h"

#include <string>
#include <vector>

namespace Mod::HeaderTool
{
std::vector<MetadataField> MetadataParser::ParseMetadata(const ExportMacro& Macro, const std::string& Source)
{
	return Parser(Macro, Source).Parse();
}
} // namespace Mod::HeaderTool
