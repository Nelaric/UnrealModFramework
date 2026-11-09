// Copyright (c) 2026 Nelaric Contributors
/** @file SourceLexer.h */
#pragma once

#include "ModHeaderTool/FileSystem.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Mod::HeaderTool::Detail
{
enum class ETokenKind
{
	End,
	Identifier,
	Literal,
	Punctuation,
};

struct FToken
{
	ETokenKind Kind = ETokenKind::End;
	std::string Text;
	char Punctuation = '\0';
	std::size_t Begin = 0;
	std::size_t End = 0;
};

class FLexer
{
public:
	FLexer(const fs::path& InPath, const std::string& InSource, std::size_t StartOffset = 0,
	       bool bInIgnoreDirectives = true);
	FToken Read();
	FToken Peek();
	std::pair<std::size_t, std::size_t> Location(std::size_t Offset) const;
	[[noreturn]] void Fail(std::size_t Offset, const std::string& Message) const;

private:
	std::size_t SkipSplices(std::size_t Offset) const;
	char Character(std::size_t Distance = 0) const;
	void Advance();
	bool Matches(char First, char Second) const;
	void SkipLineComment();
	void SkipBlockComment();
	void SkipDirective();
	void SkipTrivia();
	std::string ReadIdentifier();
	void ReadQuotedLiteral(std::size_t Begin);
	void ReadRawLiteral(std::size_t Begin);
	void ReadNumber();
	FToken ReadToken();

	const fs::path& Path;
	const std::string& Source;
	std::size_t Position = 0;
	bool bAtLineStart = true;
	bool bIgnoreDirectives = true;
	std::vector<std::size_t> LineStarts;
	std::optional<FToken> Lookahead;
};
} // namespace Mod::HeaderTool::Detail
