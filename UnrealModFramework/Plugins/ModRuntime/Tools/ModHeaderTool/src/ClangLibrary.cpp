// Copyright (c) 2026 Nelaric Contributors
#include "ClangLibrary.h"

#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Mod::HeaderTool
{
FClangLibrary::FClangLibrary(const fs::path& Path)
{
#ifdef _WIN32
	Handle = LoadLibraryW(Path.empty() ? L"libclang.dll" : Path.c_str());
#else
	Handle = dlopen(Path.empty() ? "libclang.so" : Path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
	if (!Handle)
	{
		throw std::runtime_error(
		    "Cannot load libclang. Supply --libclang <library> for the locked analysis toolchain.");
	}
	const auto Find = [this](const char* Name)
	{
#ifdef _WIN32
		return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(Handle), Name));
#else
		return dlsym(Handle, Name);
#endif
	};
	try
	{
#define MHT_CLANG_API(Name)                                                                                            \
	Name = reinterpret_cast<decltype(Name)>(Find(#Name));                                                              \
	if (!Name)                                                                                                         \
	{                                                                                                                  \
		throw std::runtime_error("libclang is missing " #Name ". Require LLVM 18 or later.");                          \
	}
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
	}
	catch (...)
	{
#ifdef _WIN32
		FreeLibrary(static_cast<HMODULE>(Handle));
#else
		dlclose(Handle);
#endif
		throw;
	}
}

FClangLibrary::~FClangLibrary()
{
	// LLVM retains process-wide worker TLS and shutdown callbacks. The CLI
	// keeps the library loaded until process teardown after disposing its TUs.
}

std::string FClangLibrary::String(CXString Value) const
{
	const char* Text = clang_getCString(Value);
	const std::string Result = Text ? Text : "";
	clang_disposeString(Value);
	return Result;
}
} // namespace Mod::HeaderTool
