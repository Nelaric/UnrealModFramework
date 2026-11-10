// Copyright (c) 2026 Nelaric Contributors
#include "AnalysisCommand.h"

#include "Common/Files/Path.h"
#include "ModSourceTool/src/Cli/AnalysisReport.h"
#include "ModSourceTool/src/Cli/ReportOutput.h"
#include "ModSourceTool/src/Errors/ErrorCatalog.h"

#include <utility>

namespace Mod::SourceTool
{
int AnalysisCommand::RunAnalysis(AnalysisOptions Options)
{
	const auto CatalogPath = Mod::BuildTool::Path::ResolvePath({}, Options.Catalog);
	const auto ReportPath = ReportOutput::ResolveReportPath(Options.Output);
	ErrorCatalog Catalog(CatalogPath, std::move(Options.MemberId), Options.bUpdateCatalog);
	const auto Request =
	    Catalog.MakeAnalysisRequest(Options.CompilationDatabase, Options.ClangLibrary, Options.ResourceDirectory,
	                                std::move(Options.Target), std::move(Options.AnalysisArguments));
	const auto Report = AnalysisReport::AnalyzeReport(Catalog, Request);
	ReportOutput::PrintDiagnostics(Report);
	const auto Output = Report.dump(2);
	Catalog.SaveChanges();
	return ReportOutput::WriteReport(ReportPath, Output);
}
} // namespace Mod::SourceTool
