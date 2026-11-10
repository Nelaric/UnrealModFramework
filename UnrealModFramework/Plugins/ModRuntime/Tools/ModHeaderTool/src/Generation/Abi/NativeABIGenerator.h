// Copyright (c) 2026 Nelaric Contributors
/** @file NativeABIGenerator.h */
#pragma once

#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::HeaderTool
{
class NativeABIGenerator
{
public:
	/// Emits a C-only client boundary, host endpoints, and connection metadata.
	static nlohmann::json WriteNativeABI(const GenerationModel& Model, std::map<std::string, std::string>& Files,
	                                     const std::string& ContractHash, const std::string& ProjectionHash,
	                                     const nlohmann::json& Lifecycle, const std::string& ErrorHeader);
};
} // namespace Mod::HeaderTool
