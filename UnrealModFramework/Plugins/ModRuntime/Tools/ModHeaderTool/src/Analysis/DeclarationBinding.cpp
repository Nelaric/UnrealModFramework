// Copyright (c) 2026 Nelaric Contributors
#include "DeclarationBinding.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/Location.h"
#include "ModHeaderTool/src/Analysis/TypeResolution.h"
#include "ModHeaderTool/src/Generation/APIParameter.h"
#include "ModHeaderTool/src/Generation/APIType.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"
#include "ModHeaderTool/src/Generation/TypeIdentity.h"
#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Scan/Lexer.h"
#include "ModHeaderTool/src/Scan/Token.h"

#include <clang-c/Index.h>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace Mod::HeaderTool
{
std::string DeclarationBinding::Field(const ExportMacro& Macro, std::string_view Name)
{
	for (const auto& Value : Macro.Fields)
	{
		if (Value.Name == Name)
		{
			return Value.Value.Text;
		}
	}
	return {};
}

bool DeclarationBinding::CheckGap(const ExportMacro& Macro, const std::string& Source, std::size_t DeclarationBegin)
{
	Detail::Lexer Lexer(Macro.File, Source, Macro.Offset + Macro.Length, false);
	bool bReflected = false;
	for (auto Token = Lexer.Read(); Token.Begin < DeclarationBegin; Token = Lexer.Read())
	{
		if (Token.Kind == Detail::Token::EKind::Identifier && Token.Text == "UFUNCTION")
		{
			bReflected = true;
			if (Lexer.Read().Punctuation != '(')
			{
				CursorSource::Fail(Macro, "malformed UFUNCTION between marker and member.");
			}
			int Depth = 1;
			while (Depth)
			{
				const auto Next = Lexer.Read();
				if (Next.Kind == Detail::Token::EKind::End)
				{
					CursorSource::Fail(Macro, "unterminated UFUNCTION.");
				}
				if (Next.Punctuation == '(')
				{
					++Depth;
				}
				if (Next.Punctuation == ')')
				{
					--Depth;
				}
			}
			continue;
		}
		CursorSource::Fail(Macro, "marker must immediately precede one supported member declaration.");
	}
	return bReflected;
}

void DeclarationBinding::BindSignature(GenerationModel& Model, ClangLibrary& Clang, CXCursor Method, API& API)
{
	if (Clang.clang_getCXXAccessSpecifier(Method) != CX_CXXPublic || Clang.clang_CXXMethod_isStatic(Method) ||
	    Clang.clang_Cursor_isVariadic(Method) || Clang.clang_CXXMethod_isDeleted(Method) ||
	    Clang.clang_Type_getCXXRefQualifier(Clang.clang_getCursorType(Method)) != CXRefQualifier_None)
	{
		CursorSource::Fail(API.Macro,
		                   "require a public, non-static, non-deleted, non-variadic member without a ref qualifier.");
	}
	const auto FunctionType = Clang.clang_getCanonicalType(Clang.clang_getCursorType(Method));
	const auto FunctionSpelling = Clang.String(Clang.clang_getTypeSpelling(FunctionType));
	const int Exception = Clang.clang_getExceptionSpecificationType(FunctionType);
	if (FunctionSpelling.find("volatile") != std::string::npos ||
	    (Exception != CXCursor_ExceptionSpecificationKind_None &&
	     Exception != CXCursor_ExceptionSpecificationKind_BasicNoexcept))
	{
		CursorSource::Fail(API.Macro, "volatile and computed exception specifications are not supported.");
	}
	API.Name = Clang.String(Clang.clang_getCursorSpelling(Method));
	if (API.Name.starts_with("operator"))
	{
		CursorSource::Fail(API.Macro, "operator exports are not supported.");
	}
	API.DeclaringType = CursorSource::QualifiedName(Clang, Clang.clang_getCursorSemanticParent(Method));
	API.bConst = Clang.clang_CXXMethod_isConst(Method) != 0;
	API.bNoexcept = Exception == CXCursor_ExceptionSpecificationKind_BasicNoexcept;
	API.SourceSignature = API.DeclaringType + "::" + API.Name + " : " + FunctionSpelling;
	API.DeclarationUsr = Clang.String(Clang.clang_getCursorUSR(Method));
	API.Return = TypeResolution::ResolveType(Model, Clang, Clang.clang_getCursorResultType(Method), API, false);
	for (int I = 0; I < Clang.clang_Cursor_getNumArguments(Method); ++I)
	{
		const auto Argument = Clang.clang_Cursor_getArgument(Method, I);
		APIParameter Parameter;
		Parameter.Name = Clang.String(Clang.clang_getCursorSpelling(Argument));
		Parameter.Type = TypeResolution::ResolveType(Model, Clang, Clang.clang_getCursorType(Argument), API, true);
		API.Parameters.push_back(std::move(Parameter));
	}
}

API DeclarationBinding::Bind(GenerationModel& Model, ClangLibrary& Clang, ExportMacro Macro, const Source& Source,
                             CXCursor Method)
{
	const auto Start =
	    CursorSource::ReadLocation(Clang, Clang.clang_getRangeStart(Clang.clang_getCursorExtent(Method)));
	const bool bReflected = CheckGap(Macro, Source.Text, Start.Offset);
	const auto& Owner = TypeResolution::ResolveRecord(Model, Clang, Clang.clang_getCursorSemanticParent(Method), Macro);
	API API;
	API.Owner = Owner.Identity.CppName;
	API.Module = Owner.Module;
	API.Version = Field(Macro, "ExportVersion");
	API.Scope = Field(Macro, "Scope");
	if (API.Scope != Owner.Identity.Scope || Field(Macro, "Thread") != "GameThread" ||
	    Field(Macro, "FailureRule") != "ReturnError")
	{
		CursorSource::Fail(Macro, "require the owner's declared scope, GameThread, and ReturnError for this backend.");
	}
	for (const auto& Item : Macro.Fields)
	{
		if (Item.Name == "Permissions")
		{
			for (const auto& Value : Item.Value.Items)
			{
				API.Permissions.push_back(Value.Text);
			}
		}
	}
	API.Macro = std::move(Macro);
	BindSignature(Model, Clang, Method, API);
	API.bReflected = bReflected || Owner.MethodNames.contains("exec" + API.Name);
	return API;
}
} // namespace Mod::HeaderTool
