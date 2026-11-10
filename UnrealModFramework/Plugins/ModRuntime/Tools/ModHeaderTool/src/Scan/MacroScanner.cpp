// Copyright (c) 2026 Nelaric Contributors
#include "MacroScanner.h"

#include "ModHeaderTool/src/Files/TextFile.h"
#include "ModHeaderTool/src/Scan/Lexer.h"
#include "ModHeaderTool/src/Scan/MacroNames.h"
#include "ModHeaderTool/src/Scan/PendingMacro.h"
#include "ModHeaderTool/src/Scan/Token.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Detail::Lexer;
using Detail::Token;

std::vector<ExportMacro> MacroScanner::ScanSource(const fs::path& Path, const std::string& Source)
{
	Lexer Lexer(Path, Source);
	std::vector<ExportMacro> Result;
	std::vector<PendingMacro> Pending;
	std::size_t Depth = 0;
	for (;;)
	{
		const Token Current = Lexer.Read();
		if (Current.Kind == Token::EKind::End)
		{
			break;
		}
		if (Current.Kind == Token::EKind::Identifier && MacroNames::IsExportMacro(Current.Text))
		{
			const Token Next = Lexer.Peek();
			if (Next.Kind == Token::EKind::Punctuation && Next.Punctuation == '(')
			{
				Lexer.Read();
				++Depth;
				Pending.push_back({Current, Next.End, Depth});
			}
		}
		else if (Current.Kind == Token::EKind::Punctuation && Current.Punctuation == '(')
		{
			++Depth;
		}
		else if (Current.Kind == Token::EKind::Punctuation && Current.Punctuation == ')')
		{
			if (!Pending.empty() && Pending.back().Depth == Depth)
			{
				const PendingMacro& Macro = Pending.back();
				const auto [Line, Column] = Lexer.Location(Macro.Token.Begin);
				Result.push_back({Macro.Token.Text, Path, Line, Column, Macro.Token.Begin,
				                  Current.End - Macro.Token.Begin,
				                  Source.substr(Macro.ArgumentsBegin, Current.Begin - Macro.ArgumentsBegin)});
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
	          [](const ExportMacro& Left, const ExportMacro& Right) { return Left.Offset < Right.Offset; });
	return Result;
}

std::vector<ExportMacro> MacroScanner::ScanFile(const fs::path& Path)
{
	return ScanSource(Path, TextFile::ReadTextFile(Path));
}
} // namespace Mod::HeaderTool
