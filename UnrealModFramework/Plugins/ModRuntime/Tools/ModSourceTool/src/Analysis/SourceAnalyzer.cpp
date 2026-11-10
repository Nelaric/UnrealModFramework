// Copyright (c) 2026 Nelaric Contributors
#include "SourceAnalyzer.h"

#include "Common/Files/Path.h"
#include "ModSourceTool/src/Analysis/Analyzer.h"
#include "ModSourceTool/src/Assembly/AssemblyAnalysis.h"
#include "ModSourceTool/src/Clang/Clang.h"
#include "ModSourceTool/src/Clang/CompilationDatabase.h"
#include "ModSourceTool/src/Clang/CompileCommand.h"
#include "ModSourceTool/src/Clang/TranslationUnitParser.h"

#include <clang-c/Index.h>
#include <memory>
#include <nlohmann/json.hpp>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

Json SourceAnalyzer::AnalyzeErrors(const SourceRequest& Request)
{
	const Clang Clang(Request.Library);
	const auto Command = CompilationDatabase::LoadCompileCommand(Clang, Request);
	const std::unique_ptr<void, decltype(Clang.clang_disposeIndex)> Index(Clang.clang_createIndex(0, 0),
	                                                                      Clang.clang_disposeIndex);
	auto Unit = TranslationUnitParser::ParseUnit(Clang, Index.get(), Command, Request.Source);
	auto Result = Analyzer(Clang, Index.get(), Unit.get(), Request, Command.Directory).Run();
	AssemblyAnalysis::AnalyzeAssembly(Request, Mod::BuildTool::Path::PathFromUtf8(Command.Compiler), Command.Directory,
	                                  Command.NativeArguments, Result);
	Result["compilationDatabase"] = Mod::BuildTool::Path::PathToUtf8(Request.Database);
	Result["workingDirectory"] = Mod::BuildTool::Path::PathToUtf8(Command.Directory);
	Result["compiler"] = Command.Compiler;
	return Result;
}
} // namespace Mod::SourceTool
