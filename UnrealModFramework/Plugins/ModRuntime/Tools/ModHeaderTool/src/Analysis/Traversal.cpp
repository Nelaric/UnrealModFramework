// Copyright (c) 2026 Nelaric Contributors
#include "Traversal.h"

#include "ModHeaderTool/src/Analysis/CursorSource.h"
#include "ModHeaderTool/src/Analysis/Location.h"
#include "ModHeaderTool/src/Files/Path.h"
#include "ModHeaderTool/src/Scan/MacroNames.h"

#include <cctype>
#include <clang-c/Index.h>
#include <exception>
#include <string>

namespace Mod::HeaderTool
{
CXChildVisitResult Traversal::Visit(CXCursor Cursor, CXCursor, CXClientData Data)
{
	auto& Context = *static_cast<Traversal*>(Data);
	try
	{
		if (Cursor.kind == CXCursor_MacroDefinition)
		{
			const auto Name = Context.Clang.String(Context.Clang.clang_getCursorSpelling(Cursor));
			if (Name.empty() || (Name.front() == '_' && Name != "_WIN64" && Name != "_M_X64") || Name.ends_with("_API"))
			{
				return CXChildVisit_Continue;
			}
			const auto File =
			    CursorSource::ReadLocation(Context.Clang, Context.Clang.clang_getCursorLocation(Cursor)).File;
			const auto Leaf = Path::PathToUtf8(File.filename());
			if (!File.empty() && Path::PathToUtf8(File).find("<command line>") == std::string::npos &&
			    !Leaf.starts_with("Definitions.") && !Leaf.starts_with("SharedDefinitions."))
			{
				return CXChildVisit_Continue;
			}
			CXToken* Tokens = nullptr;
			unsigned Count = 0;
			Context.Clang.clang_tokenize(Context.Tu, Context.Clang.clang_getCursorExtent(Cursor), &Tokens, &Count);
			if (Count == 2)
			{
				const auto Value = Context.Clang.String(Context.Clang.clang_getTokenSpelling(Context.Tu, Tokens[1]));
				if (!Value.empty() && (std::isdigit(static_cast<unsigned char>(Value.front())) ||
				                       Value.front() == '"' || Name == "UE_TARGET_NAME" ||
				                       Name == "UBT_COMPILED_PLATFORM" || Name == "UMF_MHT_TARGET_CONFIGURATION"))
				{
					Context.Macros[Name] = Value;
				}
			}
			Context.Clang.clang_disposeTokens(Context.Tu, Tokens, Count);
			return CXChildVisit_Continue;
		}
		if (Cursor.kind == CXCursor_Namespace || Cursor.kind == CXCursor_LinkageSpec)
		{
			return CXChildVisit_Recurse;
		}
		if (Cursor.kind != CXCursor_MacroExpansion && Cursor.kind != CXCursor_CXXMethod &&
		    Cursor.kind != CXCursor_ClassDecl && Cursor.kind != CXCursor_StructDecl)
		{
			return CXChildVisit_Continue;
		}
		if (Cursor.kind == CXCursor_MacroExpansion &&
		    !MacroNames::IsExportMacro(Context.Clang.String(Context.Clang.clang_getCursorSpelling(Cursor))))
		{
			return CXChildVisit_Continue;
		}
		const auto LocationValue =
		    CursorSource::ReadLocation(Context.Clang, Context.Clang.clang_getCursorLocation(Cursor));
		if (LocationValue.File.empty())
		{
			return CXChildVisit_Continue;
		}
		const auto Source = Context.SourcePaths.find(CursorSource::Key(LocationValue.File));
		if (Source == Context.SourcePaths.end())
		{
			return CXChildVisit_Continue;
		}
		if (Cursor.kind == CXCursor_MacroExpansion)
		{
			Context.ExportExpansions[Source->first].push_back(Cursor);
			return CXChildVisit_Continue;
		}
		if (Cursor.kind == CXCursor_CXXMethod)
		{
			Context.Methods[Source->first].push_back(Cursor);
			return CXChildVisit_Continue;
		}
		return CXChildVisit_Recurse;
	}
	catch (...)
	{
		Context.Error = std::current_exception();
		return CXChildVisit_Break;
	}
}
} // namespace Mod::HeaderTool
