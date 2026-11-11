// Copyright (c) 2026 Nelaric Contributors
#include "AnalysisCommand.h"

#include "Common/Files/Path.h"
#include "ModSourceTool/src/Cli/AnalysisReport.h"
#include "ModSourceTool/src/Errors/ErrorCatalog.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace Mod::SourceTool
{
int AnalysisCommand::RunAnalysis(AnalysisOptions Options, bool bUpdateCatalog)
{
	const auto CatalogPath = Mod::BuildTool::Path::ResolvePath({}, Options.Catalog);
	ErrorCatalog Catalog(CatalogPath, std::move(Options.MemberId), bUpdateCatalog);
	const auto Request =
	    Catalog.MakeAnalysisRequest(Options.CompilationDatabase, Options.ClangLibrary, Options.ResourceDirectory,
	                                std::move(Options.Target), std::move(Options.AnalysisArguments));
	const auto Report = AnalysisReport::AnalyzeReport(Catalog, Request);
	for (const auto& Diagnostic : Report.at("diagnostics"))
	{
		const auto& Location = Diagnostic.at("location");
		std::cerr << "ModSourceTool: " << Location.at("file").get<std::string>() << '('
		          << Location.at("line").get<unsigned>() << ',' << Location.at("column").get<unsigned>()
		          << "): " << Diagnostic.at("message").get<std::string>() << '\n';
	}
	const auto Output = Report.dump(2);
	Catalog.SaveChanges();
	std::cout << Output << '\n' << std::flush;
	return std::cout ? 0 : 1;
}
} // namespace Mod::SourceTool
