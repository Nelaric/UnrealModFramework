// Copyright (c) 2026 Nelaric Contributors
#include "SourceReport.h"

#include <filesystem>
#include <system_error>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

SourceReport::~SourceReport()
{
	std::error_code Ignored;
	fs::remove(File, Ignored);
	fs::remove(Request, Ignored);
}
} // namespace Mod::HeaderTool
