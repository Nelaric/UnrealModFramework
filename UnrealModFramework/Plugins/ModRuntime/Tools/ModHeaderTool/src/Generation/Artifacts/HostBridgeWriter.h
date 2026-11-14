// Copyright (c) 2026 Nelaric Contributors
/** @file HostBridgeWriter.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <map>
#include <string>

namespace Mod::HeaderTool
{
using Files = std::map<std::string, std::string>;

class HostBridgeWriter
{
public:
	static void WriteHost(const GenerationModel& Model, const std::string& ContractHash, Files& Files);
};
} // namespace Mod::HeaderTool
