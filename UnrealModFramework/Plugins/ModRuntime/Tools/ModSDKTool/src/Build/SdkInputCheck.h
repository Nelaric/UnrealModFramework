// Copyright (c) 2026 Nelaric Contributors
/** @file SdkInputCheck.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

namespace Mod::SdkTool
{
class SdkInputCheck
{
public:
	static void CheckInputs(const SdkRequest& Request);
};
} // namespace Mod::SdkTool
