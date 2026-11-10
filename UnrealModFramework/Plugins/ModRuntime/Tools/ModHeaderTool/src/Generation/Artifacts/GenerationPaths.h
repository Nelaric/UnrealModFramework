// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationPaths.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationInput.h"

#include <string>

namespace Mod::HeaderTool
{
class GenerationPaths
{
public:
	static std::string NormalizeInput(const GenerationInput& Input, std::string Text);
};
} // namespace Mod::HeaderTool
