// Copyright (c) 2026 Nelaric Contributors
/** @file BundleReuse.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

class BundleReuse
{
public:
	static Json ReuseBundle(const SdkRequest& Request);
};
} // namespace Mod::SdkTool
