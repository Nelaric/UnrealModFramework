// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/MacroScanner.h"

#include "SourceLexer.h"

#include <algorithm>

namespace Mod::HeaderTool
{
namespace
{
using Detail::ETokenKind;
using Detail::FLexer;
using Detail::FToken;

bool IsExportMacro(std::string_view Name)
{
	return Name == "MOD_EXPORT_API" || Name == "MOD_EXPORT_EVENT" || Name == "MOD_EXPORT_HOOK";
}

struct FPendingMacro
{
	FToken Token;
	std::size_t ArgumentsBegin = 0;
	std::size_t Depth = 0;
};
} // namespace

std::vector<FExportMacro> ScanSource(const fs::path& Path, const std::string& Source)
{
	FLexer Lexer(Path, Source);
	std::vector<FExportMacro> Result;
	std::vector<FPendingMacro> Pending;
	std::size_t Depth = 0;
	for (;;)
	{
		const FToken Token = Lexer.Read();
		if (Token.Kind == ETokenKind::End)
		{
			break;
		}
		if (Token.Kind == ETokenKind::Identifier && IsExportMacro(Token.Text))
		{
			const FToken Next = Lexer.Peek();
			if (Next.Kind == ETokenKind::Punctuation && Next.Punctuation == '(')
			{
				Lexer.Read();
				++Depth;
				Pending.push_back({Token, Next.End, Depth});
			}
		}
		else if (Token.Kind == ETokenKind::Punctuation && Token.Punctuation == '(')
		{
			++Depth;
		}
		else if (Token.Kind == ETokenKind::Punctuation && Token.Punctuation == ')')
		{
			if (!Pending.empty() && Pending.back().Depth == Depth)
			{
				const FPendingMacro& Macro = Pending.back();
				const auto [Line, Column] = Lexer.Location(Macro.Token.Begin);
				Result.push_back({Macro.Token.Text, Path, Line, Column, Macro.Token.Begin,
				                  Token.End - Macro.Token.Begin,
				                  Source.substr(Macro.ArgumentsBegin, Token.Begin - Macro.ArgumentsBegin)});
				Result.back().ArgumentsOffset = Macro.ArgumentsBegin;
				Pending.pop_back();
			}
			if (Depth > 0)
			{
				--Depth;
			}
		}
	}
	if (!Pending.empty())
	{
		Lexer.Fail(Pending.front().Token.Begin, "Unterminated invocation of " + Pending.front().Token.Text + ".");
	}
	std::sort(Result.begin(), Result.end(),
	          [](const FExportMacro& Left, const FExportMacro& Right) { return Left.Offset < Right.Offset; });
	return Result;
}
std::vector<FExportMacro> ScanFile(const fs::path& Path)
{
	return ScanSource(Path, ReadTextFile(Path));
}
} // namespace Mod::HeaderTool
