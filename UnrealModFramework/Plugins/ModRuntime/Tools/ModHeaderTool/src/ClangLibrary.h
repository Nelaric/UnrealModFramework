// Copyright (c) 2026 Nelaric Contributors
/** @file ClangLibrary.h */
#pragma once

#include "ModHeaderTool/FileSystem.h"

#include <clang-c/Index.h>

namespace Mod::HeaderTool
{
class FClangLibrary
{
public:
	explicit FClangLibrary(const fs::path& Path);
	~FClangLibrary();
	FClangLibrary(const FClangLibrary&) = delete;
	FClangLibrary& operator=(const FClangLibrary&) = delete;
	std::string String(CXString Value) const;

#define MHT_CLANG_API(Name) decltype(&::Name) Name = nullptr;
	MHT_CLANG_API(clang_getCString)
	MHT_CLANG_API(clang_disposeString)
	MHT_CLANG_API(clang_getClangVersion)
	MHT_CLANG_API(clang_createIndex)
	MHT_CLANG_API(clang_disposeIndex)
	MHT_CLANG_API(clang_parseTranslationUnit2FullArgv)
	MHT_CLANG_API(clang_disposeTranslationUnit)
	MHT_CLANG_API(clang_getTranslationUnitCursor)
	MHT_CLANG_API(clang_visitChildren)
	MHT_CLANG_API(clang_getCursorKind)
	MHT_CLANG_API(clang_getCursorSpelling)
	MHT_CLANG_API(clang_tokenize)
	MHT_CLANG_API(clang_disposeTokens)
	MHT_CLANG_API(clang_getTokenSpelling)
	MHT_CLANG_API(clang_getCursorUSR)
	MHT_CLANG_API(clang_getCursorSemanticParent)
	MHT_CLANG_API(clang_getCursorDefinition)
	MHT_CLANG_API(clang_getCursorLocation)
	MHT_CLANG_API(clang_getExpansionLocation)
	MHT_CLANG_API(clang_getFileName)
	MHT_CLANG_API(clang_File_tryGetRealPathName)
	MHT_CLANG_API(clang_getCursorExtent)
	MHT_CLANG_API(clang_getRangeStart)
	MHT_CLANG_API(clang_getRangeEnd)
	MHT_CLANG_API(clang_getCursorType)
	MHT_CLANG_API(clang_getCanonicalType)
	MHT_CLANG_API(clang_getTypeSpelling)
	MHT_CLANG_API(clang_getTypeDeclaration)
	MHT_CLANG_API(clang_getPointeeType)
	MHT_CLANG_API(clang_isConstQualifiedType)
	MHT_CLANG_API(clang_getCursorResultType)
	MHT_CLANG_API(clang_Cursor_getNumArguments)
	MHT_CLANG_API(clang_Cursor_getArgument)
	MHT_CLANG_API(clang_getCXXAccessSpecifier)
	MHT_CLANG_API(clang_CXXMethod_isStatic)
	MHT_CLANG_API(clang_CXXMethod_isConst)
	MHT_CLANG_API(clang_CXXMethod_isDefaulted)
	MHT_CLANG_API(clang_CXXMethod_isDeleted)
	MHT_CLANG_API(clang_Cursor_isVariadic)
	MHT_CLANG_API(clang_getExceptionSpecificationType)
	MHT_CLANG_API(clang_Type_getCXXRefQualifier)
	MHT_CLANG_API(clang_isCursorDefinition)
	MHT_CLANG_API(clang_getNumDiagnostics)
	MHT_CLANG_API(clang_getDiagnostic)
	MHT_CLANG_API(clang_getDiagnosticSeverity)
	MHT_CLANG_API(clang_formatDiagnostic)
	MHT_CLANG_API(clang_defaultDiagnosticDisplayOptions)
	MHT_CLANG_API(clang_disposeDiagnostic)
	MHT_CLANG_API(clang_getInclusions)
	MHT_CLANG_API(clang_getFile)
	MHT_CLANG_API(clang_getSkippedRanges)
	MHT_CLANG_API(clang_disposeSourceRangeList)
#undef MHT_CLANG_API

private:
	void* Handle = nullptr;
};
} // namespace Mod::HeaderTool
