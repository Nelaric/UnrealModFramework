// Copyright (c) 2026 Nelaric Contributors
#include "AnalysisReport.h"

#include "ModSourceTool/src/Analysis/SourceAnalyzer.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json AnalysisReport::AnalyzeReport(ErrorCatalog& Catalog, const SourceRequest& Request)
{
	if (!fs::is_regular_file(Request.Library) || !fs::is_directory(Request.ResourceDir / "include"))
	{
		throw std::runtime_error("Supply libclang and its matching Clang resource directory.");
	}
	auto Report = SourceAnalyzer::AnalyzeErrors(Request);
	Catalog.BindReport(Report);
	return Report;
}
} // namespace Mod::SourceTool
