// Copyright (c) 2026 Nelaric Contributors
/** @file Clang.h */
#pragma once

#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace Mod::SourceTool
{
class Clang
{
	void* Library;

	template <typename Function> Function Load(const char* Name) const
	{
		if (!Library)
		{
			throw std::runtime_error("Cannot load the requested libclang library.");
		}
#ifdef _WIN32
		const auto Symbol = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(Library), Name));
#else
		const auto Symbol = dlsym(Library, Name);
#endif
		if (!Symbol)
		{
			throw std::runtime_error(std::string("libclang is missing ") + Name + ". Use LLVM 18 or later.");
		}
		return reinterpret_cast<Function>(Symbol);
	}

public:
	// LLVM keeps process-wide state. The standalone CLI leaves the DLL loaded until exit.
	explicit Clang(const std::filesystem::path& Path);

	std::string String(CXString Value) const;

#define UMF_SOURCE_CLANG(Name) decltype(&::Name) Name = Load<decltype(&::Name)>(#Name);
	UMF_SOURCE_CLANG(clang_getCString)
	UMF_SOURCE_CLANG(clang_disposeString)
	UMF_SOURCE_CLANG(clang_CompilationDatabase_fromDirectory)
	UMF_SOURCE_CLANG(clang_CompilationDatabase_dispose)
	UMF_SOURCE_CLANG(clang_CompilationDatabase_getAllCompileCommands)
	UMF_SOURCE_CLANG(clang_CompileCommands_dispose)
	UMF_SOURCE_CLANG(clang_CompileCommands_getSize)
	UMF_SOURCE_CLANG(clang_CompileCommands_getCommand)
	UMF_SOURCE_CLANG(clang_CompileCommand_getDirectory)
	UMF_SOURCE_CLANG(clang_CompileCommand_getFilename)
	UMF_SOURCE_CLANG(clang_CompileCommand_getNumArgs)
	UMF_SOURCE_CLANG(clang_CompileCommand_getArg)
	UMF_SOURCE_CLANG(clang_createIndex)
	UMF_SOURCE_CLANG(clang_disposeIndex)
	UMF_SOURCE_CLANG(clang_parseTranslationUnit2FullArgv)
	UMF_SOURCE_CLANG(clang_disposeTranslationUnit)
	UMF_SOURCE_CLANG(clang_getNumDiagnostics)
	UMF_SOURCE_CLANG(clang_getDiagnostic)
	UMF_SOURCE_CLANG(clang_getDiagnosticSeverity)
	UMF_SOURCE_CLANG(clang_formatDiagnostic)
	UMF_SOURCE_CLANG(clang_defaultDiagnosticDisplayOptions)
	UMF_SOURCE_CLANG(clang_disposeDiagnostic)
	UMF_SOURCE_CLANG(clang_getTranslationUnitCursor)
	UMF_SOURCE_CLANG(clang_Cursor_getTranslationUnit)
	UMF_SOURCE_CLANG(clang_visitChildren)
	UMF_SOURCE_CLANG(clang_getCursorLocation)
	UMF_SOURCE_CLANG(clang_getExpansionLocation)
	UMF_SOURCE_CLANG(clang_getFile)
	UMF_SOURCE_CLANG(clang_getFileName)
	UMF_SOURCE_CLANG(clang_File_isEqual)
	UMF_SOURCE_CLANG(clang_getCursorSpelling)
	UMF_SOURCE_CLANG(clang_getCursorSemanticParent)
	UMF_SOURCE_CLANG(clang_getCursorUSR)
	UMF_SOURCE_CLANG(clang_getCursorReferenced)
	UMF_SOURCE_CLANG(clang_getCursorDefinition)
	UMF_SOURCE_CLANG(clang_equalCursors)
	UMF_SOURCE_CLANG(clang_Cursor_getVarDeclInitializer)
	UMF_SOURCE_CLANG(clang_Cursor_getStorageClass)
	UMF_SOURCE_CLANG(clang_Cursor_getMangling)
	UMF_SOURCE_CLANG(clang_getCursorExceptionSpecificationType)
	UMF_SOURCE_CLANG(clang_CXXMethod_isVirtual)
	UMF_SOURCE_CLANG(clang_getCXXAccessSpecifier)
	UMF_SOURCE_CLANG(clang_getEnumConstantDeclUnsignedValue)
	UMF_SOURCE_CLANG(clang_Cursor_getNumArguments)
	UMF_SOURCE_CLANG(clang_Cursor_getArgument)
	UMF_SOURCE_CLANG(clang_isCursorDefinition)
	UMF_SOURCE_CLANG(clang_getCursorType)
	UMF_SOURCE_CLANG(clang_getCursorResultType)
	UMF_SOURCE_CLANG(clang_getCanonicalType)
	UMF_SOURCE_CLANG(clang_getUnqualifiedType)
	UMF_SOURCE_CLANG(clang_getPointeeType)
	UMF_SOURCE_CLANG(clang_getTypeDeclaration)
	UMF_SOURCE_CLANG(clang_getTypeSpelling)
	UMF_SOURCE_CLANG(clang_isExpression)
	UMF_SOURCE_CLANG(clang_getCursorBinaryOperatorKind)
	UMF_SOURCE_CLANG(clang_getCursorUnaryOperatorKind)
	UMF_SOURCE_CLANG(clang_getCursorExtent)
	UMF_SOURCE_CLANG(clang_tokenize)
	UMF_SOURCE_CLANG(clang_disposeTokens)
	UMF_SOURCE_CLANG(clang_getTokenKind)
	UMF_SOURCE_CLANG(clang_getTokenSpelling)
	UMF_SOURCE_CLANG(clang_Cursor_Evaluate)
	UMF_SOURCE_CLANG(clang_EvalResult_getKind)
	UMF_SOURCE_CLANG(clang_EvalResult_getAsLongLong)
	UMF_SOURCE_CLANG(clang_EvalResult_isUnsignedInt)
	UMF_SOURCE_CLANG(clang_EvalResult_getAsUnsigned)
	UMF_SOURCE_CLANG(clang_EvalResult_dispose)
#undef UMF_SOURCE_CLANG
};
} // namespace Mod::SourceTool
