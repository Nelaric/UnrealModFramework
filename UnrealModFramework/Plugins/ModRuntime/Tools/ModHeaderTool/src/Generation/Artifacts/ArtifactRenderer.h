// Copyright (c) 2026 Nelaric Contributors
/** @file ArtifactRenderer.h */
#pragma once

#include "ModHeaderTool/src/Generation/Artifacts/GeneratedBatch.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

namespace Mod::HeaderTool
{
class ArtifactRenderer
{
public:
	static GeneratedBatch RenderArtifacts(GenerationModel& Model);
};
} // namespace Mod::HeaderTool
