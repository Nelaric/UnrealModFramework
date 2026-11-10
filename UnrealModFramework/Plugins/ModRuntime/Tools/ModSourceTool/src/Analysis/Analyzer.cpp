// Copyright (c) 2026 Nelaric Contributors
#include "Analyzer.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModSourceTool/src/Analysis/ArgumentValue.h"
#include "ModSourceTool/src/Analysis/Guard.h"
#include "ModSourceTool/src/Analysis/GuardClassification.h"
#include "ModSourceTool/src/Clang/CompilationDatabase.h"
#include "ModSourceTool/src/Clang/CompileCommand.h"
#include "ModSourceTool/src/Clang/TranslationUnitParser.h"

#include <algorithm>
#include <clang-c/Index.h>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Analyzer::Analyzer(const Clang& InClang, CXIndex InIndex, CXTranslationUnit InUnit, const SourceRequest& InRequest,
                   const fs::path& InCompileDirectory)
    : ClangApi(InClang), ClangIndex(InIndex), Unit(InUnit), Request(InRequest), CompileDirectory(InCompileDirectory),
      SourceFile(ClangApi.clang_getFile(Unit, Mod::BuildTool::Path::PathToUtf8(Request.Source).c_str()))
{
}

Json Analyzer::Run() const
{
	auto Info = InspectTranslationUnit();
	ValidateContext(Info.Macros);
	AnalyzedFiles.insert(Mod::BuildTool::Path::PathToUtf8(Request.Source));
	return AnalyzeDefinition(RequireDefinition(Info.Definitions), Info);
}

Analyzer::TranslationUnitInfo Analyzer::InspectTranslationUnit() const
{
	if (!SourceFile)
	{
		throw std::runtime_error("The parsed translation unit does not contain " +
		                         Mod::BuildTool::Path::PathToUtf8(Request.Source));
	}
	TranslationUnitInfo Info;
	for (const auto Child : Children(ClangApi.clang_getTranslationUnitCursor(Unit)))
	{
		if (Child.kind == CXCursor_MacroDefinition)
		{
			ReadContextMacro(Child, Info.Macros);
			continue;
		}
		if (const auto Flag = FindGarbageFlag(Child))
		{
			Info.GarbageFlag = *Flag;
		}
		FindDefinition(Child, Info.Definitions);
	}
	return Info;
}

void Analyzer::ReadContextMacro(CXCursor Child, std::map<std::string, std::string>& Macros) const
{
	const std::string Name = Spelling(Child);
	if (Name == "UE_TARGET_NAME" || Name == "UE_MODULE_NAME" || Name == "WITH_EDITOR" ||
	    Name == "PLATFORM_EXCEPTIONS_DISABLED")
	{
		const auto Tokens = Tokenize(Child);
		if (Tokens.size() != 2)
		{
			throw std::runtime_error("Expected a single UBT macro value for " + Name);
		}
		const auto [Entry, bInserted] = Macros.emplace(Name, Tokens[1]);
		if (!bInserted && Entry->second != Tokens[1])
		{
			throw std::runtime_error("Conflicting UBT macro values for " + Name);
		}
	}
}

std::optional<std::uint64_t> Analyzer::FindGarbageFlag(CXCursor Cursor) const
{
	if (Cursor.kind != CXCursor_EnumDecl || Spelling(Cursor) != "EObjectFlags" ||
	    !DeclarationFile(Cursor).ends_with("/Runtime/CoreUObject/Public/UObject/ObjectMacros.h"))
	{
		return std::nullopt;
	}
	for (const auto Constant : Children(Cursor))
	{
		if (Constant.kind == CXCursor_EnumConstantDecl && Spelling(Constant) == "RF_MirroredGarbage")
		{
			return ClangApi.clang_getEnumConstantDeclUnsignedValue(Constant);
		}
	}
	return std::nullopt;
}

void Analyzer::ValidateContext(std::map<std::string, std::string>& Macros) const
{
	const std::string Target = Macros["UE_TARGET_NAME"];
	const std::string Module = Macros["UE_MODULE_NAME"];
	const std::string WithEditor = Macros["WITH_EDITOR"];
	if (Target != Request.Target || Module != '"' + Request.Module + '"' || (WithEditor != "0" && WithEditor != "1"))
	{
		throw std::runtime_error("UBT context mismatch. Expected target " + Request.Target + ", module " +
		                         Request.Module + "; found target '" + Target + "', module " + Module +
		                         ", WITH_EDITOR='" + WithEditor + "'.");
	}
	ExpectedEditor = WithEditor;
}

CXCursor Analyzer::RequireDefinition(const std::vector<CXCursor>& Definitions) const
{
	if (Definitions.size() != 1)
	{
		throw std::runtime_error(Mod::BuildTool::Path::PathToUtf8(Request.Source) +
		                         ": expected exactly one definition of " + Request.Function + " with type " +
		                         Request.Signature + ", found " + std::to_string(Definitions.size()) + '.');
	}
	return Definitions.front();
}

void Analyzer::ValidateFailureReturn(CXCursor Definition) const
{
	const auto ResultType = ClangApi.clang_getCanonicalType(ClangApi.clang_getCursorResultType(Definition)).kind;
	if (Request.FailureReturn && ((Request.FailureReturn->is_boolean() && ResultType != CXType_Bool) ||
	                              (Request.FailureReturn->is_null() && ResultType != CXType_Pointer) ||
	                              (Request.FailureReturn->is_number_integer() && ResultType != CXType_Enum &&
	                               (ResultType < CXType_Char_U || ResultType > CXType_Int128))))
	{
		Fail(Definition, "The configured failure return does not match the definition's result type.");
	}
}

CXCursor Analyzer::FindDefinitionBody(CXCursor Definition) const
{
	for (const auto Body : Children(Definition))
	{
		if (Body.kind == CXCursor_CompoundStmt || Body.kind == CXCursor_CXXTryStmt)
		{
			return Body;
		}
	}
	Fail(Definition, "Expected an ordinary compound function body.");
}

Json Analyzer::AnalyzeDefinition(CXCursor Definition, const TranslationUnitInfo& Info) const
{
	const auto Signature = ClangApi.String(
	    ClangApi.clang_getTypeSpelling(ClangApi.clang_getCanonicalType(ClangApi.clang_getCursorType(Definition))));
	ValidateFailureReturn(Definition);
	const auto Body = FindDefinitionBody(Definition);
	const auto Macro = Info.Macros.find("PLATFORM_EXCEPTIONS_DISABLED");
	Json Diagnostics = Json::array();
	BodyAnalysis Analysis{.Body = Body,
	                      .Definition = Definition,
	                      .FailureReturn = Request.FailureReturn,
	                      .bTraceThrows = Macro == Info.Macros.end() || Macro->second != "1",
	                      .Diagnostics = Diagnostics};
	Trace Trace;
	Trace.ActiveFunctions.push_back(ClangApi.String(ClangApi.clang_getCursorUSR(Definition)));
	AnalyzeStatement(Body, Analysis, Trace);
	MarkTerminatingThrows(Analysis);
	return {{"target", Request.Target},
	        {"module", Request.Module},
	        {"withEditor", ExpectedEditor == "1"},
	        {"definition",
	         {{"function", Request.Function},
	          {"signature", Signature},
	          {"usr", ClangApi.String(ClangApi.clang_getCursorUSR(Definition))},
	          {"location", SourceLocation(Definition)}}},
	        {"assemblySymbol", ClangApi.String(ClangApi.clang_Cursor_getMangling(Definition))},
	        {"garbageFlag", Info.GarbageFlag},
	        {"diagnostics", std::move(Diagnostics)},
	        {"analyzedFiles", AnalyzedFiles},
	        {"failures", std::move(Analysis.Failures)}};
}

std::vector<CXCursor> Analyzer::Children(CXCursor Cursor) const
{
	std::vector<CXCursor> Result;
	const auto Interrupted = ClangApi.clang_visitChildren(
	    Cursor,
	    [](CXCursor Child, CXCursor, CXClientData Data)
	    {
		    try
		    {
			    static_cast<std::vector<CXCursor>*>(Data)->push_back(Child);
			    return CXChildVisit_Continue;
		    }
		    catch (...)
		    {
			    return CXChildVisit_Break;
		    }
	    },
	    &Result);
	if (Interrupted)
	{
		throw std::bad_alloc();
	}
	return Result;
}

Location Analyzer::Locate(CXCursor Cursor) const
{
	Location Result;
	ClangApi.clang_getExpansionLocation(ClangApi.clang_getCursorLocation(Cursor), &Result.File, &Result.Line,
	                                    &Result.Column, nullptr);
	return Result;
}

Json Analyzer::SourceLocation(CXCursor Cursor) const
{
	const auto Location = Locate(Cursor);
	return {{"file", DeclarationFile(Cursor)}, {"line", Location.Line}, {"column", Location.Column}};
}

[[noreturn]] void Analyzer::Fail(CXCursor Cursor, const std::string& Message) const
{
	const auto Location = Locate(Cursor);
	throw std::runtime_error(DeclarationFile(Cursor) + "(" + std::to_string(Location.Line) + "," +
	                         std::to_string(Location.Column) + "): " + Message);
}

std::string Analyzer::Spelling(CXCursor Cursor) const
{
	return ClangApi.String(ClangApi.clang_getCursorSpelling(Cursor));
}

std::string Analyzer::QualifiedName(CXCursor Cursor) const
{
	std::string Name = Spelling(Cursor);
	for (auto Parent = ClangApi.clang_getCursorSemanticParent(Cursor); Parent.kind != CXCursor_TranslationUnit;
	     Parent = ClangApi.clang_getCursorSemanticParent(Parent))
	{
		const std::string Prefix = Spelling(Parent);
		Name = Prefix + "::" + Name;
	}
	return Name;
}

void Analyzer::FindDefinition(CXCursor Cursor, std::vector<CXCursor>& Definitions) const
{
	const auto Location = Locate(Cursor);
	if (!Location.File || !ClangApi.clang_File_isEqual(Location.File, SourceFile))
	{
		return;
	}
	if (Cursor.kind == CXCursor_Namespace || Cursor.kind == CXCursor_LinkageSpec)
	{
		for (const auto Child : Children(Cursor))
		{
			FindDefinition(Child, Definitions);
		}
	}
	else if ((Cursor.kind == CXCursor_CXXMethod || Cursor.kind == CXCursor_FunctionDecl) &&
	         ClangApi.clang_isCursorDefinition(Cursor) && QualifiedName(Cursor) == Request.Function &&
	         (Request.Signature.empty() ||
	          ClangApi.String(ClangApi.clang_getTypeSpelling(
	              ClangApi.clang_getCanonicalType(ClangApi.clang_getCursorType(Cursor)))) == Request.Signature))
	{
		Definitions.push_back(Cursor);
	}
}

std::vector<std::string> Analyzer::Tokenize(CXCursor Cursor) const
{
	CXToken* Tokens = nullptr;
	unsigned Count = 0;
	const auto TokenUnit = ClangApi.clang_Cursor_getTranslationUnit(Cursor);
	ClangApi.clang_tokenize(TokenUnit, ClangApi.clang_getCursorExtent(Cursor), &Tokens, &Count);
	const auto Dispose = [&](CXToken* Value) { ClangApi.clang_disposeTokens(TokenUnit, Value, Count); };
	const std::unique_ptr<CXToken, decltype(Dispose)> Owned(Tokens, Dispose);
	std::vector<std::string> Result;
	for (unsigned Index = 0; Index < Count; ++Index)
	{
		if (ClangApi.clang_getTokenKind(Tokens[Index]) != CXToken_Comment)
		{
			Result.push_back(ClangApi.String(ClangApi.clang_getTokenSpelling(TokenUnit, Tokens[Index])));
		}
	}
	return Result;
}

CXCursor Analyzer::Unwrap(CXCursor Expression) const
{
	while (Expression.kind == CXCursor_ParenExpr || Expression.kind == CXCursor_UnexposedExpr)
	{
		const auto Parts = Children(Expression);
		if (Parts.size() != 1)
		{
			break;
		}
		Expression = Parts.front();
	}
	return Expression;
}

std::string Analyzer::DeclarationFile(CXCursor Declaration) const
{
	const auto File = Locate(Declaration).File;
	if (!File)
	{
		return {};
	}
	const auto Path = Mod::BuildTool::Path::PathFromUtf8(ClangApi.String(ClangApi.clang_getFileName(File)));
	const auto Directory = OtherDirectories.find(ClangApi.clang_Cursor_getTranslationUnit(Declaration));
	return Mod::BuildTool::Path::PathToUtf8(
	    ((Directory == OtherDirectories.end() ? CompileDirectory : Directory->second) / Path).lexically_normal());
}

std::string Analyzer::EnginePointee(CXCursor Expression) const
{
	const auto Type = ClangApi.clang_getCanonicalType(ClangApi.clang_getCursorType(Unwrap(Expression)));
	if (Type.kind != CXType_Pointer)
	{
		return {};
	}
	const auto Decl = ClangApi.clang_getTypeDeclaration(ClangApi.clang_getPointeeType(Type));
	const auto Definition = ClangApi.clang_getCursorDefinition(Decl);
	const auto File = DeclarationFile(Definition.kind == CXCursor_ClassDecl ? Definition : Decl);
	if (File.find("/Engine/Source/Runtime/Engine/") == std::string::npos)
	{
		return {};
	}
	return QualifiedName(Decl);
}

std::vector<Json> Analyzer::ClassifyNull(std::string_view Pointee) const
{
	if (Pointee == "USkeletalMesh")
	{
		return {GuardClassification::ClassifiedReason("InvalidMeshNull", "MOD_ERROR_INVALIDMESH_NULL", "201")};
	}
	if (Pointee == "USkeleton")
	{
		return {GuardClassification::ClassifiedReason("MeshHasNoSkeleton", "MOD_ERROR_MESH_SKELETON_MISSING", "203")};
	}
	if (Pointee == "USkeletalMeshComponent")
	{
		return {GuardClassification::ClassifiedReason("MeshComponentNull", "MOD_ERROR_MESH_COMPONENT_NULL", "302")};
	}
	return {};
}

CXCursor Analyzer::ReferencedVariable(CXCursor Expression) const
{
	Expression = Unwrap(Expression);
	return Expression.kind == CXCursor_DeclRefExpr ? ClangApi.clang_getCursorReferenced(Expression) : CXCursor{};
}

std::vector<Json> Analyzer::ClassifyNull(CXCursor Expression, BodyAnalysis& Analysis) const
{
	const auto Pointee = EnginePointee(Expression);
	const auto Variable = ReferencedVariable(Expression);
	if (Pointee == "USkeleton" && Variable.kind == CXCursor_VarDecl)
	{
		Analysis.NonNullSkeletons.push_back(Variable);
	}
	return ClassifyNull(Pointee);
}

std::vector<Json> Analyzer::ClassifyNegation(CXCursor Operand, BodyAnalysis& Analysis, Json& Failure) const
{
	Operand = Unwrap(Operand);
	if (auto Classifications = ClassifyNull(Operand, Analysis); !Classifications.empty())
	{
		return Classifications;
	}
	if (Operand.kind != CXCursor_CallExpr)
	{
		return {};
	}
	if (IsEngineMethod(Operand, "USkeleton::IsCompatibleMesh",
	                   "/Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h"))
	{
		return {GuardClassification::SkeletonMismatchReason()};
	}
	const auto Declaration = ClangApi.clang_getCursorReferenced(Operand);
	if (Declaration.kind != CXCursor_FunctionDecl)
	{
		return {};
	}
	const auto Function = QualifiedName(Declaration);
	const auto File = DeclarationFile(Declaration);
	const auto Arguments = ClangApi.clang_Cursor_getNumArguments(Operand);
	if (Function == "IsInGameThread" && Arguments == 0 &&
	    File.ends_with("/Engine/Source/Runtime/Core/Public/CoreGlobals.h"))
	{
		return {GuardClassification::ClassifiedReason("WrongThread", "MOD_ERROR_WRONGTHREAD")};
	}
	if (Arguments == 1 && File.ends_with("/Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h"))
	{
		const auto Pointee = EnginePointee(ClangApi.clang_Cursor_getArgument(Operand, 0));
		if ((Pointee == "USkeletalMesh" || Pointee == "USkeletalMeshComponent") &&
		    (Function == "IsValid" || Function == "IsValidChecked"))
		{
			std::vector<Json> Classifications;
			// IsValid short-circuits on null before reading the object's garbage flag.
			if (Function == "IsValid")
			{
				Classifications = ClassifyNull(Pointee);
				if (Request.bAnalyzeAssembly)
				{
					Failure["validityCall"] = {
					    {"symbol", ClangApi.String(ClangApi.clang_Cursor_getMangling(Declaration))},
					    {"file", File},
					    {"line", Locate(Declaration).Line}};
				}
			}
			Classifications.push_back(Pointee == "USkeletalMesh"
			                              ? GuardClassification::ClassifiedReason(
			                                    "InvalidMeshGarbage", "MOD_ERROR_INVALIDMESH_GARBAGE", "201")
			                              : GuardClassification::ClassifiedReason(
			                                    "MeshComponentGarbage", "MOD_ERROR_MESH_COMPONENT_GARBAGE", "302"));
			return Classifications;
		}
	}
	return {};
}

unsigned Analyzer::CountReferences(CXCursor Cursor, CXCursor Variable) const
{
	unsigned Count = 0;
	for (const auto Child : Children(Cursor))
	{
		if ((Child.kind == CXCursor_DeclRefExpr || Child.kind == CXCursor_VariableRef) &&
		    ClangApi.clang_equalCursors(ClangApi.clang_getCursorReferenced(Child), Variable))
		{
			++Count;
		}
		Count += CountReferences(Child, Variable);
	}
	return Count;
}

bool Analyzer::HasOnlyGuardReads(CXCursor Variable, CXCursor Body) const
{
	if (Variable.kind != CXCursor_VarDecl)
	{
		return false;
	}
	const auto Storage = ClangApi.clang_Cursor_getStorageClass(Variable);
	// The two matched reads account for every reference to this local pointer.
	return (Storage == CX_SC_None || Storage == CX_SC_Auto || Storage == CX_SC_Register) &&
	       CountReferences(Body, Variable) == 2;
}

bool Analyzer::IsEngineMethod(CXCursor Call, std::string_view Name, std::string_view Header) const
{
	Call = Unwrap(Call);
	if (Call.kind != CXCursor_CallExpr)
	{
		return false;
	}
	const auto Method = ClangApi.clang_getCursorReferenced(Call);
	return Method.kind == CXCursor_CXXMethod && QualifiedName(Method) == Name &&
	       DeclarationFile(Method).ends_with(Header);
}

bool Analyzer::ComparesCurrentSkeleton(CXCursor MeshPresence, CXCursor GetSkeleton, CXCursor Other,
                                       const BodyAnalysis& Analysis) const
{
	const auto Mesh = ReferencedVariable(MeshPresence);
	const auto Skeleton = ReferencedVariable(Other);
	if (Mesh.kind != CXCursor_VarDecl || Skeleton.kind != CXCursor_VarDecl ||
	    !IsEngineMethod(GetSkeleton, "USkeletalMesh::GetSkeleton", "/Runtime/Engine/Classes/Engine/SkeletalMesh.h") ||
	    !IsEngineMethod(ClangApi.clang_Cursor_getVarDeclInitializer(Mesh),
	                    "USkeletalMeshComponent::GetSkeletalMeshAsset",
	                    "/Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"))
	{
		return false;
	}
	const auto Call = Children(Unwrap(GetSkeleton));
	if (Call.size() != 1 || Call.front().kind != CXCursor_MemberRefExpr)
	{
		return false;
	}
	const auto Receiver = Children(Call.front());
	return Receiver.size() == 1 && ClangApi.clang_equalCursors(ReferencedVariable(Receiver.front()), Mesh) &&
	       std::any_of(Analysis.NonNullSkeletons.begin(), Analysis.NonNullSkeletons.end(),
	                   [&](CXCursor Checked) { return ClangApi.clang_equalCursors(Checked, Skeleton); }) &&
	       HasOnlyGuardReads(Mesh, Analysis.Body) && HasOnlyGuardReads(Skeleton, Analysis.Body);
}

std::vector<Json> Analyzer::ClassifyCondition(CXCursor Condition, BodyAnalysis& Analysis, Json& Failure) const
{
	const auto Operands = Children(Condition);
	if (Condition.kind == CXCursor_UnaryOperator && Operands.size() == 1 &&
	    ClangApi.clang_getCursorUnaryOperatorKind(Condition) == CXUnaryOperator_LNot)
	{
		return ClassifyNegation(Operands.front(), Analysis, Failure);
	}
	if (Condition.kind != CXCursor_BinaryOperator || Operands.size() != 2)
	{
		return {};
	}
	const auto Operator = ClangApi.clang_getCursorBinaryOperatorKind(Condition);
	if (Operator == CXBinaryOperator_EQ)
	{
		if (Unwrap(Operands[0]).kind == CXCursor_CXXNullPtrLiteralExpr)
		{
			return ClassifyNull(Operands[1], Analysis);
		}
		if (Unwrap(Operands[1]).kind == CXCursor_CXXNullPtrLiteralExpr)
		{
			return ClassifyNull(Operands[0], Analysis);
		}
	}
	// The left operand establishes mesh presence for the skeleton comparison.
	CXCursor MeshPresence{};
	if (Operator == CXBinaryOperator_LAnd && EnginePointee(Operands[0]) == "USkeletalMesh")
	{
		MeshPresence = Operands[0];
		Condition = Unwrap(Operands[1]);
	}
	if (Condition.kind == CXCursor_BinaryOperator &&
	    ClangApi.clang_getCursorBinaryOperatorKind(Condition) == CXBinaryOperator_NE)
	{
		const auto Compared = Children(Condition);
		if (Compared.size() == 2 && EnginePointee(Compared[0]) == "USkeleton" &&
		    EnginePointee(Compared[1]) == "USkeleton")
		{
			std::vector<Json> Classifications;
			if (ComparesCurrentSkeleton(MeshPresence, Compared[0], Compared[1], Analysis) ||
			    ComparesCurrentSkeleton(MeshPresence, Compared[1], Compared[0], Analysis))
			{
				Classifications.push_back(GuardClassification::ClassifiedReason(
				    "CurrentMeshHasNoSkeleton", "MOD_ERROR_CURRENT_MESH_SKELETON_MISSING", "203"));
			}
			Classifications.push_back(GuardClassification::SkeletonMismatchReason());
			return Classifications;
		}
	}
	return {};
}

std::string Analyzer::Expression(CXCursor Cursor) const
{
	std::string Result;
	for (const auto& Token : Tokenize(Unwrap(Cursor)))
	{
		Result += (Result.empty() ? "" : " ") + Token;
	}
	return Result;
}

std::optional<Json> Analyzer::ConstantValue(CXCursor Cursor, const Trace& Trace) const
{
	Cursor = Unwrap(Cursor);
	if (Cursor.kind == CXCursor_CXXNullPtrLiteralExpr)
	{
		return Json(nullptr);
	}
	if (Cursor.kind == CXCursor_DeclRefExpr)
	{
		const auto Reference = ClangApi.clang_getCursorReferenced(Cursor);
		for (const auto& Argument : Trace.Arguments)
		{
			if (ClangApi.clang_equalCursors(Reference, Argument.Parameter))
			{
				return Argument.Value;
			}
		}
	}
	const std::unique_ptr<void, decltype(ClangApi.clang_EvalResult_dispose)> Result(
	    ClangApi.clang_Cursor_Evaluate(Cursor), ClangApi.clang_EvalResult_dispose);
	if (Result && ClangApi.clang_EvalResult_getKind(Result.get()) == CXEval_Int)
	{
		if (ClangApi.clang_getCanonicalType(ClangApi.clang_getCursorType(Cursor)).kind == CXType_Bool)
		{
			return Json(ClangApi.clang_EvalResult_getAsLongLong(Result.get()) != 0);
		}
		return ClangApi.clang_EvalResult_isUnsignedInt(Result.get())
		           ? Json(ClangApi.clang_EvalResult_getAsUnsigned(Result.get()))
		           : Json(ClangApi.clang_EvalResult_getAsLongLong(Result.get()));
	}
	if (Cursor.kind == CXCursor_UnaryOperator &&
	    ClangApi.clang_getCursorUnaryOperatorKind(Cursor) == CXUnaryOperator_LNot)
	{
		const auto Operands = Children(Cursor);
		if (Operands.size() == 1)
		{
			if (const auto Operand = ConstantValue(Operands.front(), Trace))
			{
				return Json(Operand->is_null() || *Operand == false || *Operand == 0);
			}
		}
	}
	if (Cursor.kind == CXCursor_BinaryOperator)
	{
		const auto Op = ClangApi.clang_getCursorBinaryOperatorKind(Cursor);
		const auto Operands = Children(Cursor);
		if ((Op == CXBinaryOperator_EQ || Op == CXBinaryOperator_NE) && Operands.size() == 2)
		{
			const auto Left = ConstantValue(Operands[0], Trace);
			const auto Right = ConstantValue(Operands[1], Trace);
			if (Left && Right && Left->is_boolean() == Right->is_boolean())
			{
				return Json((*Left == *Right) == (Op == CXBinaryOperator_EQ));
			}
		}
	}
	return std::nullopt;
}

std::optional<bool> Analyzer::MatchesValue(CXCursor Cursor, const Json& Expected, const Trace& Trace) const
{
	const auto Actual = ConstantValue(Cursor, Trace);
	if (!Actual)
	{
		return std::nullopt;
	}
	if (Expected.is_boolean())
	{
		const bool bValue = !Actual->is_null() && *Actual != false && *Actual != 0;
		return bValue == Expected.get<bool>();
	}
	return Expected.is_null() ? Actual->is_null() || *Actual == 0 : *Actual == Expected;
}

void Analyzer::Diagnose(BodyAnalysis& Analysis, CXCursor Where, const std::string& Message) const
{
	const Json Entry = {{"location", SourceLocation(Where)}, {"message", Message}};
	if (std::find(Analysis.Diagnostics.begin(), Analysis.Diagnostics.end(), Entry) == Analysis.Diagnostics.end())
	{
		Analysis.Diagnostics.push_back(Entry);
	}
}

Json Analyzer::Exit(CXCursor At, CXCursor Condition, std::string Guard, const Trace& Trace,
                    const BodyAnalysis& Analysis, const char* Kind) const
{
	Json Chain = Json::array();
	for (const auto Call : Trace.Calls)
	{
		const auto Callee = ClangApi.clang_getCursorReferenced(Call);
		Chain.push_back({{"location", SourceLocation(Call)},
		                 {"function", QualifiedName(Callee)},
		                 {"symbol", ClangApi.String(ClangApi.clang_Cursor_getMangling(Callee))}});
	}
	Json Guards = Json::array();
	for (const auto& Item : Trace.Guards)
	{
		Guards.push_back({{"expression", Expression(Item.Condition)}, {"value", Item.bTrue}});
	}
	return {{"guard", std::move(Guard)},
	        {"conditionLocation", SourceLocation(Condition)},
	        {"returnLocation", SourceLocation(At)},
	        {"exitKind", Kind},
	        {"originFunction", QualifiedName(Analysis.Definition)},
	        {"callChain", std::move(Chain)},
	        {"pathConditions", std::move(Guards)}};
}

Json Analyzer::DerivedReason(const BodyAnalysis& Analysis, std::string_view Kind, const std::string& Description,
                             std::string_view Exception) const
{
	const auto Name = QualifiedName(Analysis.Definition);
	const auto Signature =
	    ClangApi.String(ClangApi.clang_getTypeSpelling(ClangApi.clang_getCursorType(Analysis.Definition)));
	const auto Identity =
	    Mod::BuildTool::Hash::HashBytes(Json::array({Kind, Name, Signature, Exception, Description}).dump());
	std::string Macro = "MOD_ERROR_";
	for (const char Ch : Name + "_" + std::string(Kind) + "_" + Description)
	{
		if (Macro.size() >= 96)
		{
			break;
		}
		if ((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9'))
		{
			Macro += Ch >= 'a' && Ch <= 'z' ? static_cast<char>(Ch - 'a' + 'A') : Ch;
		}
		else if (!Macro.ends_with('_'))
		{
			Macro += '_';
		}
	}
	if (!Macro.ends_with('_'))
	{
		Macro += '_';
	}
	for (const char Ch : std::string_view(Identity).substr(0, 10))
	{
		Macro += Ch >= 'a' && Ch <= 'f' ? static_cast<char>(Ch - 'a' + 'A') : Ch;
	}
	auto Result = GuardClassification::ClassifiedReason((std::string(Kind) + '/' + Identity).c_str(), Macro.c_str(),
	                                                    Kind == "Throw" ? "802" : "801");
	Result["description"] = Description;
	return Result;
}

CXType Analyzer::ExceptionType(CXType Type) const
{
	Type = ClangApi.clang_getCanonicalType(Type);
	if (Type.kind == CXType_LValueReference || Type.kind == CXType_RValueReference)
	{
		Type = ClangApi.clang_getPointeeType(Type);
	}
	return ClangApi.clang_getUnqualifiedType(Type);
}

std::string Analyzer::ExceptionName(CXType Type) const
{
	return ClangApi.String(ClangApi.clang_getTypeSpelling(ExceptionType(Type)));
}

void Analyzer::PublicBases(CXType Type, std::map<std::string, unsigned>& Bases) const
{
	const auto Decl = ClangApi.clang_getTypeDeclaration(ExceptionType(Type));
	if (Decl.kind != CXCursor_ClassDecl && Decl.kind != CXCursor_StructDecl)
	{
		return;
	}
	const auto Definition = ClangApi.clang_getCursorDefinition(Decl);
	for (const auto Base : Children(Definition))
	{
		if (Base.kind == CXCursor_CXXBaseSpecifier && ClangApi.clang_getCXXAccessSpecifier(Base) == CX_CXXPublic)
		{
			const auto BaseType = ClangApi.clang_getCursorType(Base);
			++Bases[ExceptionName(BaseType)];
			PublicBases(BaseType, Bases);
		}
	}
}

} // namespace Mod::SourceTool
