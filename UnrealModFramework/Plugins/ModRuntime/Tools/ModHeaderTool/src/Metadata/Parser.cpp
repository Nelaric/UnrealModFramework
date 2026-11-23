// Copyright (c) 2026 Nelaric Contributors
#include "Parser.h"

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
using Detail::Token;

Parser::Parser(const ExportMacro& InMacro, const std::string& InSource)
    : Macro(InMacro), Source(InSource), Lexer(Macro.File, Source, Macro.ArgumentsOffset, false)
{
}

std::vector<MetadataField> Parser::Parse()
{
	std::vector<MetadataField> Result;
	while (!IsPunctuation(Lexer.Peek(), ')'))
	{
		const Token Name = Lexer.Read();
		if (Name.Kind != Token::EKind::Identifier)
		{
			Fail(Name.Begin, "expected a field name.");
		}
		Expect('=');
		MetadataField Field;
		Field.Name = Name.Text;
		Field.Offset = Name.Begin;
		Field.Value = ParseValue();
		Result.push_back(std::move(Field));
		if (IsPunctuation(Lexer.Peek(), ')'))
		{
			break;
		}
		Expect(',');
	}
	const Token Close = Lexer.Read();
	if (Close.Begin != Macro.ArgumentsOffset + Macro.Arguments.size())
	{
		Fail(Close.Begin, "unexpected closing parenthesis in metadata.");
	}
	return Result;
}

int Parser::HexDigit(char Character)
{
	if (Character >= '0' && Character <= '9')
	{
		return Character - '0';
	}
	if (Character >= 'a' && Character <= 'f')
	{
		return Character - 'a' + 10;
	}
	if (Character >= 'A' && Character <= 'F')
	{
		return Character - 'A' + 10;
	}
	return -1;
}

bool Parser::IsPunctuation(const Token& Token, char Character)
{
	return Token.Kind == Token::EKind::Punctuation && Token.Punctuation == Character;
}

[[noreturn]] void Parser::Fail(std::size_t Offset, const std::string& Reason) const
{
	Lexer.Fail(Offset, Macro.Name + ": " + Reason);
}

void Parser::Expect(char Character)
{
	const Token Token = Lexer.Read();
	if (!IsPunctuation(Token, Character))
	{
		Fail(Token.Begin, std::string("expected '") + Character + "'.");
	}
}

std::string Parser::LiteralPrefix(const Token& Token) const
{
	const std::string_view Spelling(Source.data() + Token.Begin, Token.End - Token.Begin);
	const std::size_t Quote = Spelling.find('"');
	if (Quote == std::string_view::npos)
	{
		return "?";
	}
	std::string Prefix;
	for (std::size_t Index = 0; Index < Quote; ++Index)
	{
		if (Spelling[Index] == '\\' && Index + 1 < Quote && Spelling[Index + 1] == '\n')
		{
			++Index;
		}
		else if (Spelling[Index] == '\\' && Index + 2 < Quote && Spelling[Index + 1] == '\r' &&
		         Spelling[Index + 2] == '\n')
		{
			Index += 2;
		}
		else
		{
			Prefix.push_back(Spelling[Index]);
		}
	}
	return Prefix;
}

bool Parser::IsString(const Token& Token) const
{
	if (Token.Kind != Token::EKind::Literal)
	{
		return false;
	}
	const std::string Prefix = LiteralPrefix(Token);
	return Prefix.empty() || Prefix == "u8" || Prefix == "u" || Prefix == "U" || Prefix == "L" || Prefix == "R" ||
	       Prefix == "u8R" || Prefix == "uR" || Prefix == "UR" || Prefix == "LR";
}

void Parser::AppendCodePoint(std::string& Result, std::uint32_t Value, std::size_t Offset) const
{
	if (Value > 0x10ffff || (Value >= 0xd800 && Value <= 0xdfff))
	{
		Fail(Offset, "invalid Unicode scalar in string literal.");
	}
	if (Value <= 0x7f)
	{
		Result.push_back(static_cast<char>(Value));
	}
	else if (Value <= 0x7ff)
	{
		Result.push_back(static_cast<char>(0xc0 | (Value >> 6)));
		Result.push_back(static_cast<char>(0x80 | (Value & 0x3f)));
	}
	else if (Value <= 0xffff)
	{
		Result.push_back(static_cast<char>(0xe0 | (Value >> 12)));
		Result.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3f)));
		Result.push_back(static_cast<char>(0x80 | (Value & 0x3f)));
	}
	else
	{
		Result.push_back(static_cast<char>(0xf0 | (Value >> 18)));
		Result.push_back(static_cast<char>(0x80 | ((Value >> 12) & 0x3f)));
		Result.push_back(static_cast<char>(0x80 | ((Value >> 6) & 0x3f)));
		Result.push_back(static_cast<char>(0x80 | (Value & 0x3f)));
	}
}

std::string Parser::DecodeString(const Token& Token) const
{
	const std::string_view Original(Source.data() + Token.Begin, Token.End - Token.Begin);
	const std::string Prefix = LiteralPrefix(Token);
	std::string Logical;
	std::vector<std::size_t> Offsets;
	for (std::size_t Index = 0; Index < Original.size(); ++Index)
	{
		if (!Prefix.ends_with("R") && Original[Index] == '\\' && Index + 1 < Original.size())
		{
			if (Original[Index + 1] == '\n')
			{
				++Index;
				continue;
			}
			if (Original[Index + 1] == '\r' && Index + 2 < Original.size() && Original[Index + 2] == '\n')
			{
				Index += 2;
				continue;
			}
		}
		Logical.push_back(Original[Index]);
		Offsets.push_back(Token.Begin + Index);
	}
	Offsets.push_back(Token.End);
	const std::string_view Spelling(Logical);
	const std::size_t Quote = Spelling.find('"');
	std::string Result;
	std::size_t End = 0;
	if (Prefix.ends_with("R"))
	{
		const std::size_t Open = Spelling.find('(', Quote + 1);
		const std::string Delimiter(Spelling.substr(Quote + 1, Open - Quote - 1));
		const std::string Terminator = ")" + Delimiter + "\"";
		const std::size_t Close = Spelling.find(Terminator, Open + 1);
		const std::string_view Body = Spelling.substr(Open + 1, Close - Open - 1);
		Result.reserve(Body.size());
		for (std::size_t Index = 0; Index < Body.size(); ++Index)
		{
			if (Body[Index] == '\r')
			{
				if (Index + 1 < Body.size() && Body[Index + 1] == '\n')
				{
					++Index;
				}
				Result.push_back('\n');
			}
			else
			{
				Result.push_back(Body[Index]);
			}
		}
		End = Close + Terminator.size();
	}
	else
	{
		std::size_t Index = Quote + 1;
		while (Index < Spelling.size() && Spelling[Index] != '"')
		{
			const std::size_t Offset = Offsets[Index];
			if (Spelling[Index] != '\\')
			{
				Result.push_back(Spelling[Index++]);
				continue;
			}
			++Index;
			const char Escape = Spelling[Index++];
			switch (Escape)
			{
			case '\n':
				break;
			case '\r':
				if (Index < Spelling.size() && Spelling[Index] == '\n')
				{
					++Index;
					break;
				}
				Fail(Offset, "invalid line splice.");
			case '\\':
				Result.push_back('\\');
				break;
			case '"':
				Result.push_back('"');
				break;
			case '\'':
				Result.push_back('\'');
				break;
			case '?':
				Result.push_back('?');
				break;
			case 'a':
				Result.push_back('\a');
				break;
			case 'b':
				Result.push_back('\b');
				break;
			case 'f':
				Result.push_back('\f');
				break;
			case 'n':
				Result.push_back('\n');
				break;
			case 'r':
				Result.push_back('\r');
				break;
			case 't':
				Result.push_back('\t');
				break;
			case 'v':
				Result.push_back('\v');
				break;
			case 'u':
			case 'U':
			{
				const std::size_t Digits = Escape == 'u' ? 4 : 8;
				std::uint32_t Value = 0;
				for (std::size_t Digit = 0; Digit < Digits; ++Digit)
				{
					if (Index >= Spelling.size() || HexDigit(Spelling[Index]) < 0)
					{
						Fail(Offset, "invalid Unicode escape.");
					}
					Value = (Value << 4) | static_cast<std::uint32_t>(HexDigit(Spelling[Index++]));
				}
				AppendCodePoint(Result, Value, Offset);
				break;
			}
			case 'x':
			{
				std::uint32_t Value = 0;
				if (Index >= Spelling.size() || HexDigit(Spelling[Index]) < 0)
				{
					Fail(Offset, "hexadecimal escape requires digits.");
				}
				while (Index < Spelling.size() && HexDigit(Spelling[Index]) >= 0)
				{
					if (Value > 0x0fffffff)
					{
						Fail(Offset, "hexadecimal escape is out of range.");
					}
					Value = (Value << 4) | static_cast<std::uint32_t>(HexDigit(Spelling[Index++]));
				}
				if (Prefix.empty() || Prefix == "u8")
				{
					if (Value > 255)
					{
						Fail(Offset, "hexadecimal byte escape exceeds 255.");
					}
					Result.push_back(static_cast<char>(Value));
				}
				else
				{
					AppendCodePoint(Result, Value, Offset);
				}
				break;
			}
			default:
				if (Escape < '0' || Escape > '7')
				{
					Fail(Offset, std::string("unsupported escape '\\") + Escape + "'.");
				}
				std::uint32_t Value = static_cast<std::uint32_t>(Escape - '0');
				for (int Digit = 1;
				     Digit < 3 && Index < Spelling.size() && Spelling[Index] >= '0' && Spelling[Index] <= '7'; ++Digit)
				{
					Value = (Value << 3) | static_cast<std::uint32_t>(Spelling[Index++] - '0');
				}
				if (Value > 255)
				{
					Fail(Offset, "octal byte escape exceeds 255.");
				}
				Result.push_back(static_cast<char>(Value));
				break;
			}
		}
		End = Index + 1;
	}
	if (End != Spelling.size())
	{
		Fail(Offsets[End], "user-defined string suffixes are not supported in metadata.");
	}
	if (Result.find('\0') != std::string::npos)
	{
		Fail(Token.Begin, "metadata strings must not contain NUL characters.");
	}
	try
	{
		(void)nlohmann::json(Result).dump();
	}
	catch (const nlohmann::json::exception&)
	{
		Fail(Token.Begin, "string literal must encode valid UTF-8.");
	}
	return Result;
}

MetadataValue Parser::ParseScalar()
{
	const Token Scalar = Lexer.Read();
	MetadataValue Result;
	Result.Offset = Scalar.Begin;
	if (IsString(Scalar))
	{
		Result.Text = DecodeString(Scalar);
		while (IsString(Lexer.Peek()))
		{
			Result.Text += DecodeString(Lexer.Read());
		}
	}
	else if (Scalar.Kind == Token::EKind::Identifier && Scalar.Text == "TEXT" && IsPunctuation(Lexer.Peek(), '('))
	{
		Expect('(');
		if (!IsString(Lexer.Peek()))
		{
			Fail(Lexer.Peek().Begin, "TEXT requires a string literal.");
		}
		Result = ParseScalar();
		Result.Offset = Scalar.Begin;
		Expect(')');
	}
	else if (Scalar.Kind == Token::EKind::Identifier)
	{
		Result.Kind = Scalar.Text == "true" || Scalar.Text == "false" || Scalar.Text == "nullptr"
		                  ? MetadataValue::EKind::Literal
		                  : MetadataValue::EKind::Identifier;
		Result.Text = Scalar.Text;
		while (IsPunctuation(Lexer.Peek(), ':'))
		{
			Expect(':');
			Expect(':');
			const Token Part = Lexer.Read();
			if (Part.Kind != Token::EKind::Identifier)
			{
				Fail(Part.Begin, "expected an identifier after '::'.");
			}
			Result.Text += "::" + Part.Text;
		}
	}
	else if (Scalar.Kind == Token::EKind::Literal)
	{
		Result.Kind = MetadataValue::EKind::Literal;
		Result.Text = Source.substr(Scalar.Begin, Scalar.End - Scalar.Begin);
	}
	else
	{
		Fail(Scalar.Begin, "expected a string, identifier, or parenthesized list; expressions are not supported.");
	}
	return Result;
}

MetadataValue Parser::ParseValue()
{
	if (!IsPunctuation(Lexer.Peek(), '('))
	{
		return ParseScalar();
	}
	MetadataValue Result;
	Result.Kind = MetadataValue::EKind::List;
	Result.Offset = Lexer.Read().Begin;
	while (!IsPunctuation(Lexer.Peek(), ')'))
	{
		Result.Items.push_back(ParseScalar());
		if (IsPunctuation(Lexer.Peek(), ')'))
		{
			break;
		}
		Expect(',');
	}
	Expect(')');
	return Result;
}
} // namespace Mod::HeaderTool
