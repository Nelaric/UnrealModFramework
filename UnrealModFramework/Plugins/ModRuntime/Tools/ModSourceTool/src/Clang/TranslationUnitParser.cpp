// Copyright (c) 2026 Nelaric Contributors
#include "TranslationUnitParser.h"

#include "Common/Files/Path.h"

#include <clang-c/Index.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using TranslationUnit = std::unique_ptr<CXTranslationUnitImpl, decltype(&::clang_disposeTranslationUnit)>;

TranslationUnit TranslationUnitParser::ParseUnit(const Clang& Clang, CXIndex Index, const CompileCommand& Command,
                                                 const fs::path& Source)
{
	std::vector<const char*> Arguments;
	for (const auto& Argument : Command.Arguments)
	{
		Arguments.push_back(Argument.c_str());
	}
	CXTranslationUnit Parsed = nullptr;
	const auto Error =
	    Clang.clang_parseTranslationUnit2FullArgv(Index, nullptr, Arguments.data(), static_cast<int>(Arguments.size()),
	                                              nullptr, 0, CXTranslationUnit_DetailedPreprocessingRecord, &Parsed);
	TranslationUnit Unit(Parsed, Clang.clang_disposeTranslationUnit);
	if (Error != CXError_Success || !Unit)
	{
		throw std::runtime_error("Cannot parse " + Mod::BuildTool::Path::PathToUtf8(Source) +
		                         " with its compile command (Clang " + std::to_string(Error) + ").");
	}
	for (unsigned I = 0; I < Clang.clang_getNumDiagnostics(Unit.get()); ++I)
	{
		const std::unique_ptr<void, decltype(Clang.clang_disposeDiagnostic)> Diagnostic(
		    Clang.clang_getDiagnostic(Unit.get(), I), Clang.clang_disposeDiagnostic);
		if (Clang.clang_getDiagnosticSeverity(Diagnostic.get()) >= CXDiagnostic_Error)
		{
			throw std::runtime_error(Clang.String(
			    Clang.clang_formatDiagnostic(Diagnostic.get(), Clang.clang_defaultDiagnosticDisplayOptions())));
		}
	}
	return Unit;
}
} // namespace Mod::SourceTool
