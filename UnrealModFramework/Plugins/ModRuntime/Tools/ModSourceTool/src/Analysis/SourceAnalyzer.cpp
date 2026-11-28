// Copyright (c) 2026 Nelaric Contributors
#include "SourceAnalyzer.h"

#include "Common/Errors/ErrorCodes.h"
#include "Common/Files/Path.h"
#include "ModSourceTool/src/Analysis/Analyzer.h"
#include "ModSourceTool/src/Assembly/AssemblyAnalysis.h"
#include "ModSourceTool/src/Clang/Clang.h"
#include "ModSourceTool/src/Clang/CompilationDatabase.h"
#include "ModSourceTool/src/Clang/CompileCommand.h"
#include "ModSourceTool/src/Clang/TranslationUnitParser.h"
#include "ModSourceTool/src/Errors/ErrorCodePolicy.h"

#include <clang-c/Index.h>
#include <map>
#include <memory>
#include <stdexcept>

namespace Mod::SourceTool
{
Json SourceAnalyzer::AnalyzeErrors(const std::vector<SourceRequest>& Requests)
{
	using Mod::BuildTool::Path;
	const auto& Shared = Requests.front();
	if (!fs::is_regular_file(Shared.Library) || !fs::is_directory(Shared.ResourceDir / "include"))
	{
		throw std::runtime_error("Supply libclang and its matching Clang resource directory.");
	}
	const Clang Clang(Shared.Library);
	const std::unique_ptr<void, decltype(Clang.clang_disposeIndex)> Index(Clang.clang_createIndex(0, 0),
	                                                                      Clang.clang_disposeIndex);
	std::map<fs::path, std::vector<const SourceRequest*>> Groups;
	for (const auto& Request : Requests)
	{
		Groups[Request.TranslationUnit].push_back(&Request);
	}
	Json Reports = Json::array();
	ErrorCodePolicy Codes;
	for (const auto& [File, Group] : Groups)
	{
		const auto Command = CompilationDatabase::LoadCompileCommand(Clang, *Group.front());
		auto Unit = TranslationUnitParser::ParseUnit(Clang, Index.get(), Command, File);
		std::optional<std::string> Listing;
		for (const auto* Request : Group)
		{
			auto Report = Analyzer(Clang, Index.get(), Unit.get(), *Request, Command.Directory).Run();
			AssemblyAnalysis::AnalyzeAssembly(*Request, Path::PathFromUtf8(Command.Compiler), Command.Directory,
			                                  Command.NativeArguments, Listing, Report);
			Codes.Bind(Report.at("failures"));
			Report["memberId"] = Request->MemberId;
			Report["errorCodeEncodingVersion"] = Tools::ErrorCodes::StructuredErrorCodeVersion;
			Report["compilationDatabase"] = Path::PathToUtf8(Request->Database);
			Report["workingDirectory"] = Path::PathToUtf8(Command.Directory);
			Report["compiler"] = Command.Compiler;
			Reports.push_back(std::move(Report));
		}
	}
	return {{"format", "UMF.SourceAnalysisReport"}, {"schemaVersion", 2}, {"members", std::move(Reports)}};
}
} // namespace Mod::SourceTool
