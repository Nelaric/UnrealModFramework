// Copyright (c) 2026 Nelaric Contributors
/** @file BlueprintExports.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

namespace Mod::HeaderTool::BlueprintExports
{
void Merge(GenerationModel& Model, const fs::path& Snapshot);
} // namespace Mod::HeaderTool::BlueprintExports
