// Copyright (c) 2026 Nelaric Contributors
/** @file ExtensionWriter.h */
#pragma once

#include <map>
#include <string>

namespace Mod::HeaderTool
{
struct GenerationModel;

void WriteExtensions(const GenerationModel& Model, std::map<std::string, std::string>& Files);
} // namespace Mod::HeaderTool
