// Copyright (c) 2026 Nelaric Contributors
/** @file SdkViewWriter.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <map>
#include <string>

namespace Mod::HeaderTool
{
using Files = std::map<std::string, std::string>;

class SdkViewWriter
{
public:
	static void WriteSdk(const GenerationModel& Model, Files& Files);
};
} // namespace Mod::HeaderTool
