// Copyright (c) 2026 Nelaric Contributors
#include "HookSourceWriter.h"

#include "Common/Files/File.h"
#include "Common/Files/Path.h"
#include "ModSourceTool/src/Clang/CompilationDatabase.h"
#include "ModSourceTool/src/Clang/TranslationUnitParser.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
namespace
{
using Mod::BuildTool::File;
using Mod::BuildTool::Path;

struct Edit
{
	std::size_t Begin;
	std::size_t End;
	std::string Replacement;
};

struct SourcePosition
{
	CXFile File = nullptr;
	unsigned Line = 0;
	unsigned Offset = 0;
};

SourcePosition SourceLocation(const Clang& Clang, CXSourceLocation Location)
{
	SourcePosition Position;
	Clang.clang_getSpellingLocation(Location, &Position.File, &Position.Line, nullptr, &Position.Offset);
	return Position;
}

std::vector<CXCursor> Children(const Clang& Clang, CXCursor Parent)
{
	std::vector<CXCursor> Result;
	Clang.clang_visitChildren(
	    Parent,
	    [](CXCursor Child, CXCursor, CXClientData Data)
	    {
		    static_cast<std::vector<CXCursor>*>(Data)->push_back(Child);
		    return CXChildVisit_Continue;
	    },
	    &Result);
	return Result;
}

struct SourceEdits
{
	const Clang& Compiler;
	const CXFile MainFile;
	const fs::path& Source;
	std::string_view Bytes;
	const std::map<std::string, std::string>& Dispatchers;
	std::set<std::string>& Found;
	std::vector<Edit> Edits;

	bool IsMainFile(CXFile File) const
	{
		return File && Compiler.clang_File_isEqual(File, MainFile);
	}

	void Visit(CXCursor Parent)
	{
		for (const auto Cursor : Children(Compiler, Parent))
		{
			if (!IsMainFile(SourceLocation(Compiler, Compiler.clang_getCursorLocation(Cursor)).File))
			{
				continue;
			}
			if (Cursor.kind == CXCursor_Namespace)
			{
				Visit(Cursor);
			}
			else if (Cursor.kind == CXCursor_InclusionDirective)
			{
				RebaseLocalInclude(Cursor);
			}
			else if (Cursor.kind == CXCursor_CXXMethod && Compiler.clang_isCursorDefinition(Cursor))
			{
				const auto Usr = Compiler.String(Compiler.clang_getCursorUSR(Cursor));
				if (const auto Hook = Dispatchers.find(Usr); Hook != Dispatchers.end())
				{
					if (!Found.insert(Usr).second)
					{
						throw std::runtime_error("Automatic Hook has multiple C++ definitions: " + Usr);
					}
					WrapDefinition(Cursor, Hook->second);
				}
			}
		}
	}

	void RebaseLocalInclude(CXCursor Cursor)
	{
		const auto Name = Path::PathFromUtf8(Compiler.String(Compiler.clang_getCursorSpelling(Cursor)));
		if (Name.is_absolute() || !fs::is_regular_file(Source.parent_path() / Name))
		{
			return;
		}
		const auto Range = Compiler.clang_getCursorExtent(Cursor);
		const auto Begin = SourceLocation(Compiler, Compiler.clang_getRangeStart(Range));
		const auto End = SourceLocation(Compiler, Compiler.clang_getRangeEnd(Range));
		if (!IsMainFile(Begin.File) || !IsMainFile(End.File) || End.Offset > Bytes.size() || Begin.Offset >= End.Offset)
		{
			return;
		}
		const auto Text = Bytes.substr(Begin.Offset, End.Offset - Begin.Offset);
		const auto Quote = Text.find('"');
		if (Quote == Text.npos)
		{
			return;
		}
		const auto Close = Text.find('"', Quote + 1);
		if (Close != Text.npos && Text.substr(Quote + 1, Close - Quote - 1) == Path::PathToUtf8(Name))
		{
			Edits.push_back({Begin.Offset + Quote + 1, Begin.Offset + Close,
			                 Path::PathToUtf8(fs::canonical(Source.parent_path() / Name))});
		}
	}

	void WrapDefinition(CXCursor Cursor, const std::string& Dispatcher)
	{
		const auto Name = Compiler.String(Compiler.clang_getCursorSpelling(Cursor));
		const auto Fail = [&]
		{
			throw std::runtime_error(Path::PathToUtf8(Source) + ": automatic Hook '" + Name +
			                         "' requires a named scalar parameter and a directly written function body.");
		};
		if (Compiler.clang_Cursor_getNumArguments(Cursor) != 1)
		{
			Fail();
		}
		const auto Parameter = Compiler.clang_Cursor_getArgument(Cursor, 0);
		const auto ParameterName = Compiler.String(Compiler.clang_getCursorSpelling(Parameter));
		const auto ParameterAt = SourceLocation(Compiler, Compiler.clang_getCursorLocation(Parameter));
		if (ParameterName.empty() || !IsMainFile(ParameterAt.File) ||
		    Bytes.substr(ParameterAt.Offset, ParameterName.size()) != ParameterName)
		{
			Fail();
		}
		const auto Nodes = Children(Compiler, Cursor);
		const auto Body = std::ranges::find(Nodes, CXCursor_CompoundStmt, &CXCursor::kind);
		if (Body == Nodes.end())
		{
			Fail();
		}
		const auto Range = Compiler.clang_getCursorExtent(*Body);
		const auto Begin = SourceLocation(Compiler, Compiler.clang_getRangeStart(Range));
		const auto End = SourceLocation(Compiler, Compiler.clang_getRangeEnd(Range));
		if (!IsMainFile(Begin.File) || !IsMainFile(End.File) || End.Offset > Bytes.size() ||
		    Begin.Offset >= End.Offset || Bytes[Begin.Offset] != '{' || Bytes[End.Offset - 1] != '}')
		{
			Fail();
		}
		const auto TypeSpelling = [&](CXType Type)
		{ return Compiler.String(Compiler.clang_getTypeSpelling(Compiler.clang_getCanonicalType(Type))); };
		const auto PathText = Json(Path::PathToUtf8(Source)).dump();
		const auto InputName = ParameterName + "_UMFHookInput";
		Edits.push_back({ParameterAt.Offset, ParameterAt.Offset + ParameterName.size(), InputName});
		Edits.push_back({Begin.Offset, End.Offset,
		                 "{\n    return " + Dispatcher + "(*this, " + InputName + ", [this](" +
		                     TypeSpelling(Compiler.clang_getCursorType(Parameter)) + " " + ParameterName + ") -> " +
		                     TypeSpelling(Compiler.clang_getCursorResultType(Cursor)) + "\n#line " +
		                     std::to_string(Begin.Line) + " " + PathText + "\n" +
		                     std::string(Bytes.substr(Begin.Offset, End.Offset - Begin.Offset)) +
		                     "\n    );\n}\n#line " + std::to_string(End.Line) + " " + PathText + "\n"});
	}
};
} // namespace

void HookSourceWriter::Rewrite(const fs::path& RequestFile)
{
	const auto Request = Json::parse(File::ReadFile(RequestFile));
	if (Request.value("format", "") != "UMF.HookSourceRequest" || Request.value("schemaVersion", 0) != 1)
	{
		throw std::runtime_error("Expected a UMF.HookSourceRequest v1 document.");
	}
	SourceRequest Options;
	Options.Database = Path::PathFromUtf8(Request.at("compilationDatabase").get<std::string>());
	Options.ResourceDir = Path::PathFromUtf8(Request.at("resourceDirectory").get<std::string>());
	Options.AnalysisArguments = Request.at("analysisArguments").get<std::vector<std::string>>();
	const Clang Compiler(Path::PathFromUtf8(Request.at("libclang").get<std::string>()));
	const std::unique_ptr<void, decltype(Compiler.clang_disposeIndex)> Index(Compiler.clang_createIndex(0, 0),
	                                                                         Compiler.clang_disposeIndex);
	Json Output = Json::array();
	for (const auto& Module : Request.at("modules"))
	{
		const auto Owner = Module.at("owner").get<std::string>();
		const auto Dispatchers = Module.at("dispatchers").get<std::map<std::string, std::string>>();
		std::set<std::string> Found;
		for (const auto& Source : Module.at("sources"))
		{
			const auto SourceName = Source.get<std::string>();
			Options.Source = fs::canonical(Path::PathFromUtf8(SourceName));
			auto Bytes = File::ReadFile(Options.Source);
			const auto Command = CompilationDatabase::LoadCompileCommand(Compiler, Options);
			const auto Unit = TranslationUnitParser::ParseUnit(Compiler, Index.get(), Command, Options.Source);
			SourceEdits Work{Compiler,
			                 Compiler.clang_getFile(Unit.get(), SourceName.c_str()),
			                 Options.Source,
			                 Bytes,
			                 Dispatchers,
			                 Found,
			                 {}};
			const auto FoundBefore = Found.size();
			Work.Visit(Compiler.clang_getTranslationUnitCursor(Unit.get()));
			if (Found.size() == FoundBefore)
			{
				continue;
			}
			const auto Original = Bytes;
			std::ranges::sort(Work.Edits, std::greater{}, &Edit::Begin);
			for (const auto& Edit : Work.Edits)
			{
				Bytes.replace(Edit.Begin, Edit.End - Edit.Begin, Edit.Replacement);
			}
			const std::size_t Start = Bytes.starts_with("\xef\xbb\xbf") ? 3 : 0;
			Bytes.insert(Start,
			             "#include \"" + Owner + ".modextensions.h\"\n#line 1 " + Json(SourceName).dump() + "\n");
			if (Original != File::ReadFile(Options.Source))
			{
				throw std::runtime_error("Hook source changed during analysis: " + SourceName);
			}
			Output.push_back({{"owner", Owner}, {"source", SourceName}, {"original", Original}, {"code", Bytes}});
		}
		for (const auto& [Usr, Dispatcher] : Dispatchers)
		{
			if (!Found.contains(Usr))
			{
				throw std::runtime_error("Missing out-of-line definition for " + Dispatcher + " in " + Owner +
				                         " analysis translation units.");
			}
		}
	}
	std::cout << Output.dump() << '\n';
}
} // namespace Mod::SourceTool
