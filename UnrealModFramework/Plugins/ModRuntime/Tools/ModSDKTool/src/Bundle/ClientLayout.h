// Copyright (c) 2026 Nelaric Contributors
/** @file ClientLayout.h */
#pragma once

#include "ModSDKTool/src/Manifest/Library.h"

#include <string>

namespace Mod::SdkTool
{
class ClientLayout
{
public:
	static std::string ClientRelativePath(const Library& Job);
};
} // namespace Mod::SdkTool
