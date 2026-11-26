// Copyright (c) 2026 Nelaric Contributors
/** @file TargetArtifacts.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

namespace Mod::SdkTool
{
class TargetArtifacts
{
public:
	/// Validates per-target host bindings and collects the shared Runtime's inputs.
	static void ReadTargets(SdkRequest& Request);
};
} // namespace Mod::SdkTool
