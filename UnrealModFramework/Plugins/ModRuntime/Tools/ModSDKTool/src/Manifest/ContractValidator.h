// Copyright (c) 2026 Nelaric Contributors
/** @file ContractValidator.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

namespace Mod::SdkTool
{
class ContractValidator
{
public:
	static void ValidateContract(SdkRequest& Request);
	static void ValidateConnectionDescriptor(const SdkRequest& Request);
};
} // namespace Mod::SdkTool
