// Copyright (c) 2026 Nelaric Contributors
/** @file SourceReportBinder.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/GenerationModel.h"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class SourceReportBinder
{
public:
	static void BindSourceErrors(API& API, GenerationModel& Model, const Json& Member, const Json& ClassifiedReasons,
	                             const Json& Report, const fs::path& Database);

private:
	static bool SameFile(const Json& Report, const char* Key, const fs::path& File);
};
} // namespace Mod::HeaderTool
