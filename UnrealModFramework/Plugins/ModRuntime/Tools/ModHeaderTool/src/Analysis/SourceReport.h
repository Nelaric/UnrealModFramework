// Copyright (c) 2026 Nelaric Contributors
/** @file SourceReport.h */
#pragma once

#include "Common/Files/TemporaryName.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct SourceReport
{
	const fs::path File =
	    fs::temp_directory_path() / ("umf-mht-source-" + Mod::BuildTool::TemporaryName::UniqueSuffix() + ".json");
	const fs::path Request = fs::path(File).replace_extension(".request.json");

	~SourceReport();
};
} // namespace Mod::HeaderTool
