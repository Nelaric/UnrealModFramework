// Copyright (c) 2026 Nelaric Contributors
/** @file APIParameter.h */
#pragma once

#include "ModHeaderTool/src/Generation/APIType.h"

#include <string>

namespace Mod::HeaderTool
{
struct APIParameter
{
	std::string Name;
	APIType Type;
};
} // namespace Mod::HeaderTool
