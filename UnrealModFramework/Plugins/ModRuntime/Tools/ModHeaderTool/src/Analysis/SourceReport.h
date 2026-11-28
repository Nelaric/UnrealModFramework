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
	explicit SourceReport(const fs::path& Directory)
	    : File(Directory / ("umf-mht-source-" + Mod::BuildTool::TemporaryName::UniqueSuffix() + ".json")),
	      Request(fs::path(File).replace_extension(".request.json"))
	{
	}

	const fs::path File;
	const fs::path Request;

	~SourceReport();
};
} // namespace Mod::HeaderTool
