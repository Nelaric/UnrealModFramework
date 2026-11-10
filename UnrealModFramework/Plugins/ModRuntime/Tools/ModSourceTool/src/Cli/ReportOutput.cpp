// Copyright (c) 2026 Nelaric Contributors
#include "ReportOutput.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

fs::path ReportOutput::ResolveReportPath(const std::string& Output)
{
	const auto Path = Output.empty() ? fs::path{} : Mod::BuildTool::Path::ResolvePath({}, Output);
	if (!Path.empty() && fs::exists(Path))
	{
		throw std::runtime_error("Report output already exists: " + Mod::BuildTool::Path::PathToUtf8(Path));
	}
	return Path;
}

void ReportOutput::PrintDiagnostics(const Json& Report)
{
	for (const auto& Diagnostic : Report.at("diagnostics"))
	{
		const auto& Location = Diagnostic.at("location");
		std::cerr << "ModSourceTool: " << Location.at("file").get<std::string>() << '('
		          << Location.at("line").get<unsigned>() << ',' << Location.at("column").get<unsigned>()
		          << "): " << Diagnostic.at("message").get<std::string>() << '\n';
	}
}

int ReportOutput::WriteReport(const fs::path& ReportPath, const std::string& Output)
{
	if (!ReportPath.empty())
	{
		Mod::BuildTool::File::WriteFileAtomically(ReportPath, Output + '\n');
		return 0;
	}
	std::cout << Output << '\n';
	return std::cout ? 0 : 1;
}
} // namespace Mod::SourceTool
