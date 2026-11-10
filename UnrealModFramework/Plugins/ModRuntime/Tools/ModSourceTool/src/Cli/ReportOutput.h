// Copyright (c) 2026 Nelaric Contributors
/** @file ReportOutput.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class ReportOutput
{
public:
	static fs::path ResolveReportPath(const std::string& Output);
	static void PrintDiagnostics(const Json& Report);
	static int WriteReport(const fs::path& ReportPath, const std::string& Output);
};
} // namespace Mod::SourceTool
