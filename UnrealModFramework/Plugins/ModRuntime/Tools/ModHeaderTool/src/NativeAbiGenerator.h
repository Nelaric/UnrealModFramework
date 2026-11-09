// Copyright (c) 2026 Nelaric Contributors
/** @file NativeAbiGenerator.h */
#pragma once

#include "GenerationModel.h"

#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
/// Emits a C-only client boundary, host endpoints, and connection metadata.
nlohmann::json WriteNativeAbi(const FGenerationModel& Model, std::map<std::string, std::string>& Files,
                              const std::string& ContractHash, const std::string& ProjectionHash);
} // namespace Mod::HeaderTool
