// Copyright (c) 2026 Nelaric Contributors
/** @file ExportNames.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"
#include "ModHeaderTool/src/Metadata/MetadataOptions.h"

namespace Mod::HeaderTool
{
class ExportNames
{
public:
	static void AssignExportNames(GenerationModel& Model, const MetadataOptions& Options);
};
} // namespace Mod::HeaderTool
