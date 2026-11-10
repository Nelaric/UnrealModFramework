// Copyright (c) 2026 Nelaric Contributors
#include "MetadataValidator.h"

#include "ModHeaderTool/src/Metadata/Validator.h"

#include <string>
#include <vector>

namespace Mod::HeaderTool
{
std::vector<std::string> MetadataValidator::ValidateMetadata(ExportMacro& Macro, const MetadataOptions* Options,
                                                             const std::string& Source)
{
	return Validator(Macro, Options, Source).Validate();
}
} // namespace Mod::HeaderTool
