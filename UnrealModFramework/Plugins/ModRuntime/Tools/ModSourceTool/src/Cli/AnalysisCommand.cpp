// Copyright (c) 2026 Nelaric Contributors
#include "AnalysisCommand.h"

#include "ModSourceTool/src/Analysis/SourceAnalyzer.h"

#include <iostream>

namespace Mod::SourceTool
{
int AnalysisCommand::RunAnalysis(const AnalysisOptions& Options)
{
	const auto Report = SourceAnalyzer::AnalyzeErrors(Options.Requests);
	for (const auto& Member : Report.at("members"))
	{
		for (const auto& Diagnostic : Member.at("diagnostics"))
		{
			const auto& Location = Diagnostic.at("location");
			std::cerr << "ModSourceTool: " << Location.at("file").get<std::string>() << '('
			          << Location.at("line").get<unsigned>() << ',' << Location.at("column").get<unsigned>()
			          << "): " << Diagnostic.at("message").get<std::string>() << '\n';
		}
	}
	std::cout << Report.dump(2) << '\n' << std::flush;
	return std::cout ? 0 : 1;
}
} // namespace Mod::SourceTool
