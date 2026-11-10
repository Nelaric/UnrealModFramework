// Copyright (c) 2026 Nelaric Contributors
/** @file Parser.h */
#pragma once

#include "ModHeaderTool/src/Metadata/MetadataField.h"
#include "ModHeaderTool/src/Metadata/MetadataValue.h"
#include "ModHeaderTool/src/Scan/ExportMacro.h"
#include "ModHeaderTool/src/Scan/Lexer.h"
#include "ModHeaderTool/src/Scan/Token.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
using Detail::Token;

class Parser
{
public:
	Parser(const ExportMacro& InMacro, const std::string& InSource);

	std::vector<MetadataField> Parse();

private:
	static int HexDigit(char Character);

	static bool IsPunctuation(const Token& Token, char Character);

	[[noreturn]] void Fail(std::size_t Offset, const std::string& Reason) const;

	void Expect(char Character);

	std::string LiteralPrefix(const Token& Token) const;

	bool IsString(const Token& Token) const;

	void AppendCodePoint(std::string& Result, std::uint32_t Value, std::size_t Offset) const;

	std::string DecodeString(const Token& Token) const;

	MetadataValue ParseScalar();

	MetadataValue ParseValue();

	const ExportMacro& Macro;
	const std::string& Source;
	Detail::Lexer Lexer;
};
} // namespace Mod::HeaderTool
