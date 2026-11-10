// Copyright (c) 2026 Nelaric Contributors
/** @file Analyzer.h */
#pragma once

#include "ModSourceTool/src/Analysis/BodyAnalysis.h"
#include "ModSourceTool/src/Analysis/Location.h"
#include "ModSourceTool/src/Analysis/SourceRequest.h"
#include "ModSourceTool/src/Analysis/Trace.h"
#include "ModSourceTool/src/Clang/Clang.h"

#include <clang-c/Index.h>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
using TranslationUnit = std::unique_ptr<CXTranslationUnitImpl, decltype(&::clang_disposeTranslationUnit)>;

class Analyzer
{
public:
	Analyzer(const Clang& InClang, CXIndex InIndex, CXTranslationUnit InUnit, const SourceRequest& InRequest,
	         const fs::path& InCompileDirectory);

	Json Run() const;

private:
	struct TranslationUnitInfo
	{
		std::map<std::string, std::string> Macros;
		std::vector<CXCursor> Definitions;
		std::uint64_t GarbageFlag = 0;
	};

	TranslationUnitInfo InspectTranslationUnit() const;

	void ReadContextMacro(CXCursor Child, std::map<std::string, std::string>& Macros) const;

	std::optional<std::uint64_t> FindGarbageFlag(CXCursor Cursor) const;

	void ValidateContext(std::map<std::string, std::string>& Macros) const;

	CXCursor RequireDefinition(const std::vector<CXCursor>& Definitions) const;

	void ValidateFailureReturn(CXCursor Definition) const;

	CXCursor FindDefinitionBody(CXCursor Definition) const;

	Json AnalyzeDefinition(CXCursor Definition, const TranslationUnitInfo& Info) const;

	std::vector<CXCursor> Children(CXCursor Cursor) const;

	Location Locate(CXCursor Cursor) const;

	Json SourceLocation(CXCursor Cursor) const;

	[[noreturn]] void Fail(CXCursor Cursor, const std::string& Message) const;

	std::string Spelling(CXCursor Cursor) const;

	std::string QualifiedName(CXCursor Cursor) const;

	void FindDefinition(CXCursor Cursor, std::vector<CXCursor>& Definitions) const;

	std::vector<std::string> Tokenize(CXCursor Cursor) const;

	CXCursor Unwrap(CXCursor Expression) const;

	std::string DeclarationFile(CXCursor Declaration) const;

	std::string EnginePointee(CXCursor Expression) const;

	std::vector<Json> ClassifyNull(std::string_view Pointee) const;

	CXCursor ReferencedVariable(CXCursor Expression) const;

	std::vector<Json> ClassifyNull(CXCursor Expression, BodyAnalysis& Analysis) const;

	std::vector<Json> ClassifyNegation(CXCursor Operand, BodyAnalysis& Analysis, Json& Failure) const;

	unsigned CountReferences(CXCursor Cursor, CXCursor Variable) const;

	bool HasOnlyGuardReads(CXCursor Variable, CXCursor Body) const;

	bool IsEngineMethod(CXCursor Call, std::string_view Name, std::string_view Header) const;

	bool ComparesCurrentSkeleton(CXCursor MeshPresence, CXCursor GetSkeleton, CXCursor Other,
	                             const BodyAnalysis& Analysis) const;

	std::vector<Json> ClassifyCondition(CXCursor Condition, BodyAnalysis& Analysis, Json& Failure) const;

	std::string Expression(CXCursor Cursor) const;

	std::optional<Json> ConstantValue(CXCursor Cursor, const Trace& Trace) const;

	std::optional<bool> MatchesValue(CXCursor Cursor, const Json& Expected, const Trace& Trace) const;

	void Diagnose(BodyAnalysis& Analysis, CXCursor Where, const std::string& Message) const;

	Json Exit(CXCursor At, CXCursor Condition, std::string Guard, const Trace& Trace, const BodyAnalysis& Analysis,
	          const char* Kind) const;

	Json DerivedReason(const BodyAnalysis& Analysis, std::string_view Kind, const std::string& Description,
	                   std::string_view Exception = {}) const;

	CXType ExceptionType(CXType Type) const;

	std::string ExceptionName(CXType Type) const;

	void PublicBases(CXType Type, std::map<std::string, unsigned>& Bases) const;

	void AnalyzeThrow(CXCursor Throw, BodyAnalysis& Analysis, const Trace& Trace) const;

	bool IsNoexcept(CXCursor Function) const;

	void MarkTerminatingThrows(BodyAnalysis& Analysis) const;

	bool OwnsPath(const fs::path& File) const;

	void IndexDefinitions(CXCursor Cursor, CXFile File) const;

	CXCursor ProjectDefinition(const std::string& Usr, CXCursor Call, BodyAnalysis& Analysis) const;

	bool FollowCall(CXCursor Call, std::optional<Json> FailureReturn, bool bTraceThrows, BodyAnalysis& Analysis,
	                Trace Trace) const;

	void ObserveExpression(CXCursor Cursor, BodyAnalysis& Analysis, const Trace& Trace) const;

	void AddAlternatives(CXCursor Condition, CXCursor Return, Json& Prior, BodyAnalysis& Analysis,
	                     const Trace& Trace) const;

	void EmitReturn(CXCursor Return, CXCursor Value, const Json& Expected, BodyAnalysis& Analysis, const Trace& Trace,
	                bool bConstant) const;

	void AnalyzeValue(CXCursor Return, CXCursor Value, const Json& Expected, BodyAnalysis& Analysis, Trace Trace) const;

	bool CatchMatches(CXCursor Handler, const Json& Failure) const;

	bool AnalyzeTry(const std::vector<CXCursor>& Parts, BodyAnalysis& Analysis, const Trace& Trace) const;

	bool AnalyzeStatement(CXCursor Statement, BodyAnalysis& Analysis, Trace& Trace) const;

	const Clang& ClangApi;
	CXIndex ClangIndex;
	CXTranslationUnit Unit;
	const SourceRequest& Request;
	const fs::path& CompileDirectory;
	CXFile SourceFile;
	mutable std::string ExpectedEditor;
	mutable std::set<std::string> AnalyzedFiles;
	mutable bool bIndexedProject = false;
	mutable std::vector<TranslationUnit> OtherUnits;
	mutable std::map<CXTranslationUnit, fs::path> OtherDirectories;
	mutable std::map<std::string, std::vector<CXCursor>> OtherDefinitions;
};
} // namespace Mod::SourceTool
