// Copyright (c) 2026 Nelaric Contributors
#include "Source.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Scan/Lexer.h"
#include "ModHeaderTool/src/Scan/Token.h"

#include <memory>
#include <stdexcept>

namespace Mod::HeaderTool
{
namespace
{
void Mask(std::string& Text, Source::Range Range)
{
	// Preserve physical lines and byte offsets for the AST and diagnostics.
	for (std::size_t Offset = Range.Begin; Offset < Range.End; ++Offset)
	{
		if (Text[Offset] != '\r' && Text[Offset] != '\n')
		{
			Text[Offset] = ' ';
		}
	}
}
} // namespace

Source::Source(ClangLibrary& Clang, CXTranslationUnit Tu, CXFile InFile, const fs::path& InPath)
    : Path(InPath), File(InFile)
{
	std::size_t Size = 0;
	const char* Contents = Clang.clang_getFileContents(Tu, File, &Size);
	if (!Contents)
	{
		throw std::runtime_error("Could not read Clang source buffer for " +
		                         DiagnosticLocation::QuoteDiagnosticPath(Path));
	}
	Text.assign(Contents, Size);
	MaskConditionalDirectives(Clang, Tu);
	if (!ConditionalGroups.empty())
	{
		TextWithoutConditionalDirectives = Text;
	}

	const std::unique_ptr<CXSourceRangeList, decltype(Clang.clang_disposeSourceRangeList)> Skipped(
	    Clang.clang_getSkippedRanges(Tu, File), Clang.clang_disposeSourceRangeList);
	for (unsigned I = 0; I < Skipped->count; ++I)
	{
		if (const auto Range = Offsets(Clang, Skipped->ranges[I]))
		{
			Mask(Text, *Range);
		}
	}
}

void Source::MaskConditionalDirectives(ClangLibrary& Clang, CXTranslationUnit Tu)
{
	const auto Begin = Clang.clang_getLocationForOffset(Tu, File, 0);
	const auto End = Clang.clang_getLocationForOffset(Tu, File, static_cast<unsigned>(Text.size()));
	CXToken* Tokens = nullptr;
	unsigned Count = 0;
	Clang.clang_tokenize(Tu, Clang.clang_getRange(Begin, End), &Tokens, &Count);
	const auto Dispose = [&](CXToken* Value) { Clang.clang_disposeTokens(Tu, Value, Count); };
	const std::unique_ptr<CXToken, decltype(Dispose)> Cleanup(Tokens, Dispose);
	std::vector<CXCursor> Cursors(Count);
	Clang.clang_annotateTokens(Tu, Tokens, Count, Cursors.data());

	// Clang's raw tokens retain directive boundaries, including in skipped branches.
	std::vector<std::size_t> OpenGroups;
	std::size_t DirectiveEnd = 0;
	for (unsigned I = 0; I < Count; ++I)
	{
		if (Cursors[I].kind != CXCursor_PreprocessingDirective)
		{
			continue;
		}
		const auto Range = Offsets(Clang, Clang.clang_getCursorExtent(Cursors[I]));
		if (!Range || Range->Begin < DirectiveEnd)
		{
			continue;
		}
		DirectiveEnd = Range->End;
		unsigned Keyword = I + 1;
		while (Keyword < Count && Clang.clang_getTokenKind(Tokens[Keyword]) == CXToken_Comment)
		{
			++Keyword;
		}
		if (Keyword == Count ||
		    Offset(Clang, Clang.clang_getTokenLocation(Tu, Tokens[Keyword])).value_or(Text.size()) >= DirectiveEnd)
		{
			continue;
		}
		const auto Name = Clang.String(Clang.clang_getTokenSpelling(Tu, Tokens[Keyword]));
		if (Name == "if" || Name == "ifdef" || Name == "ifndef")
		{
			OpenGroups.push_back(Range->Begin);
		}
		else if (Name == "endif" && !OpenGroups.empty())
		{
			ConditionalGroups.push_back({OpenGroups.back(), Range->End});
			OpenGroups.pop_back();
		}
		else if (Name != "elif" && Name != "elifdef" && Name != "elifndef" && Name != "else")
		{
			continue;
		}
		Mask(Text, *Range);
	}
}

std::optional<std::size_t> Source::Offset(ClangLibrary& Clang, CXSourceLocation Location) const
{
	CXFile LocationFile = nullptr;
	unsigned ByteOffset = 0;
	Clang.clang_getExpansionLocation(Location, &LocationFile, nullptr, nullptr, &ByteOffset);
	if (LocationFile != File || ByteOffset > Text.size())
	{
		return std::nullopt;
	}
	return ByteOffset;
}

std::optional<Source::Range> Source::Offsets(ClangLibrary& Clang, CXSourceRange Extent) const
{
	const auto Begin = Offset(Clang, Clang.clang_getRangeStart(Extent));
	const auto End = Offset(Clang, Clang.clang_getRangeEnd(Extent));
	if (!Begin || !End || *Begin >= *End)
	{
		return std::nullopt;
	}
	return Range{*Begin, *End};
}

std::optional<ExportMacro> Source::ReadMacro(ClangLibrary& Clang, CXCursor Expansion) const
{
	const auto Range = Offsets(Clang, Clang.clang_getCursorExtent(Expansion));
	// The preprocessing record also contains references such as defined(MOD_EXPORT_API).
	if (!Range || Text[Range->End - 1] != ')')
	{
		return std::nullopt;
	}
	Detail::Lexer Lexer(Path, Text, Range->Begin);
	const auto Name = Lexer.Read();
	if (Name.Begin != Range->Begin || Name.Kind != Detail::Token::EKind::Identifier ||
	    Name.Text != Clang.String(Clang.clang_getCursorSpelling(Expansion)))
	{
		return std::nullopt;
	}
	const auto Open = Lexer.Read();
	if (Open.Punctuation != '(' || Open.End >= Range->End)
	{
		return std::nullopt;
	}
	const auto [Line, Column] = Lexer.Location(Name.Begin);
	ExportMacro Macro{Name.Text,
	                  Path,
	                  Line,
	                  Column,
	                  Name.Begin,
	                  Range->End - Name.Begin,
	                  Text.substr(Open.End, Range->End - Open.End - 1)};
	Macro.ArgumentsOffset = Open.End;
	return Macro;
}
} // namespace Mod::HeaderTool
