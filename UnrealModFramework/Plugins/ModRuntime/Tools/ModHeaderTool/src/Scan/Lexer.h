// Copyright (c) 2026 Nelaric Contributors
/** @file Lexer.h */
#pragma once

#include "ModHeaderTool/src/Scan/Token.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Mod::HeaderTool::Detail
{
namespace fs = std::filesystem;

class Lexer
{
public:
	Lexer(const fs::path& InPath, const std::string& InSource, std::size_t StartOffset = 0,
	      bool bInIgnoreDirectives = true);
	Token Read();
	Token Peek();
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
	Token ReadToken();

	const fs::path& Path;
	const std::string& Source;
	std::size_t Position = 0;
	bool bAtLineStart = true;
	bool bIgnoreDirectives = true;
	std::vector<std::size_t> LineStarts;
	std::optional<Token> Lookahead;
};
} // namespace Mod::HeaderTool::Detail
