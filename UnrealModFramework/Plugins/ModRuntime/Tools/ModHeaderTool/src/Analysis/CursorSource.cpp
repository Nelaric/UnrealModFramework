// Copyright (c) 2026 Nelaric Contributors
#include "CursorSource.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Files/Path.h"

#include <algorithm>
#include <cctype>
#include <clang-c/Index.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

std::string CursorSource::Key(const fs::path& Path)
{
	std::string Text = Path::PathToUtf8(fs::absolute(Path).lexically_normal());
#ifdef _WIN32
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return std::tolower(C); });
#endif
	return Text;
}

Location CursorSource::ReadLocation(const ClangLibrary& Clang, CXSourceLocation Source, bool bResolvePath)
{
	CXFile File = nullptr;
	Location Result;
	Clang.clang_getExpansionLocation(Source, &File, nullptr, nullptr, &Result.Offset);
	if (File)
	{
		const std::string RealPath = bResolvePath ? Clang.String(Clang.clang_File_tryGetRealPathName(File)) : "";
		Result.File = Path::PathFromUtf8(RealPath.empty() ? Clang.String(Clang.clang_getFileName(File)) : RealPath);
	}
	return Result;
}

std::string CursorSource::QualifiedName(const ClangLibrary& Clang, CXCursor Cursor)
{
	std::string Result = Clang.String(Clang.clang_getCursorSpelling(Cursor));
	for (CXCursor Parent = Clang.clang_getCursorSemanticParent(Cursor);
	     Parent.kind != CXCursor_TranslationUnit && Parent.kind != CXCursor_InvalidFile;
	     Parent = Clang.clang_getCursorSemanticParent(Parent))
	{
		if (Parent.kind != CXCursor_Namespace)
		{
			throw std::runtime_error(
			    "Nested classes and class templates are not supported by the UObject API backend.");
		}
		const auto Name = Clang.String(Clang.clang_getCursorSpelling(Parent));
		if (Name.empty())
		{
			throw std::runtime_error("Anonymous namespaces cannot own exported types.");
		}
		Result = Name + "::" + Result;
	}
	return Result;
}

[[noreturn]] void CursorSource::Fail(const ExportMacro& Macro, const std::string& Reason)
{
	throw std::runtime_error(DiagnosticLocation::FormatDiagnosticLocation(Macro.File, Macro.Line, Macro.Column) +
	                         ": error: " + Macro.Name + ": " + Reason);
}
} // namespace Mod::HeaderTool
