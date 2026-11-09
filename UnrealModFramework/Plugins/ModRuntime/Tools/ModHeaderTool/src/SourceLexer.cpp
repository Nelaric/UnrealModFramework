// Copyright (c) 2026 Nelaric Contributors
#include "SourceLexer.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Mod::HeaderTool::Detail
{
namespace
{
bool IsIdentifierStart(char Character)
{
	return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || Character == '_' ||
	       static_cast<unsigned char>(Character) >= 0x80;
}

bool IsDigit(char Character)
{
	return Character >= '0' && Character <= '9';
}

bool IsIdentifierContinue(char Character)
{
	return IsIdentifierStart(Character) || IsDigit(Character);
}

bool IsWhitespace(char Character)
{
	return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n' || Character == '\v' ||
	       Character == '\f';
}

} // namespace

FLexer::FLexer(const fs::path& InPath, const std::string& InSource, std::size_t StartOffset, bool bInIgnoreDirectives)
    : Path(InPath), Source(InSource), bIgnoreDirectives(bInIgnoreDirectives)
{
	const std::size_t Nul = Source.find('\0');
	const std::size_t First = Source.starts_with("\xef\xbb\xbf") ? 3 : 0;
	Position = std::max(First, StartOffset);
	bAtLineStart = Position == First;
	LineStarts.push_back(First);
	for (std::size_t Index = First; Index < Source.size(); ++Index)
	{
		if (Source[Index] == '\r')
		{
			if (Index + 1 < Source.size() && Source[Index + 1] == '\n')
			{
				++Index;
			}
			LineStarts.push_back(Index + 1);
		}
		else if (Source[Index] == '\n')
		{
			LineStarts.push_back(Index + 1);
		}
	}
	if (Nul != std::string::npos)
	{
		Fail(Nul, "source contains NUL bytes; use UTF-8, not UTF-16.");
	}
	for (std::size_t Index = 0; Index < Source.size();)
	{
		const auto Byte = [this](std::size_t Offset) { return static_cast<unsigned char>(Source[Offset]); };
		const unsigned char FirstByte = Byte(Index);
		if (FirstByte < 0x80)
		{
			++Index;
			continue;
		}
		const std::size_t Length = FirstByte >= 0xc2 && FirstByte <= 0xdf   ? 2
		                           : FirstByte >= 0xe0 && FirstByte <= 0xef ? 3
		                           : FirstByte >= 0xf0 && FirstByte <= 0xf4 ? 4
		                                                                    : 0;
		if (Length == 0 || Index + Length > Source.size())
		{
			Fail(Index, "source must encode valid UTF-8.");
		}
		for (std::size_t Tail = 1; Tail < Length; ++Tail)
		{
			if (Byte(Index + Tail) < 0x80 || Byte(Index + Tail) > 0xbf)
			{
				Fail(Index + Tail, "invalid UTF-8 continuation byte.");
			}
		}
		const unsigned char SecondByte = Byte(Index + 1);
		if ((FirstByte == 0xe0 && SecondByte < 0xa0) || (FirstByte == 0xed && SecondByte > 0x9f) ||
		    (FirstByte == 0xf0 && SecondByte < 0x90) || (FirstByte == 0xf4 && SecondByte > 0x8f))
		{
			Fail(Index, "invalid UTF-8 scalar encoding.");
		}
		Index += Length;
	}
}

FToken FLexer::Read()
{
	if (Lookahead)
	{
		FToken Token = std::move(*Lookahead);
		Lookahead.reset();
		return Token;
	}
	SkipTrivia();
	return ReadToken();
}

FToken FLexer::Peek()
{
	if (!Lookahead)
	{
		Lookahead = Read();
	}
	return *Lookahead;
}

std::pair<std::size_t, std::size_t> FLexer::Location(std::size_t Offset) const
{
	const auto NextLine = std::upper_bound(LineStarts.begin(), LineStarts.end(), Offset);
	const std::size_t Line = static_cast<std::size_t>(NextLine - LineStarts.begin());
	return {Line, Offset - LineStarts[Line - 1] + 1};
}

[[noreturn]] void FLexer::Fail(std::size_t Offset, const std::string& Message) const
{
	const auto [Line, Column] = Location(Offset);
	throw std::runtime_error(PathToUtf8(Path) + "(" + std::to_string(Line) + "," + std::to_string(Column) +
	                         "): error: " + Message);
}

std::size_t FLexer::SkipSplices(std::size_t Offset) const
{
	while (Offset + 1 < Source.size() && Source[Offset] == '\\')
	{
		if (Source[Offset + 1] == '\n')
		{
			Offset += 2;
		}
		else if (Source[Offset + 1] == '\r' && Offset + 2 < Source.size() && Source[Offset + 2] == '\n')
		{
			Offset += 3;
		}
		else
		{
			break;
		}
	}
	return Offset;
}

char FLexer::Character(std::size_t Distance) const
{
	std::size_t Offset = SkipSplices(Position);
	while (Distance > 0 && Offset < Source.size())
	{
		Offset = SkipSplices(Offset + 1);
		--Distance;
	}
	return Offset < Source.size() ? Source[Offset] : '\0';
}

void FLexer::Advance()
{
	Position = SkipSplices(Position);
	if (Position < Source.size())
	{
		++Position;
	}
}

bool FLexer::Matches(char First, char Second) const
{
	return Character() == First && Character(1) == Second;
}

void FLexer::SkipLineComment()
{
	Advance();
	Advance();
	while (Character() != '\0' && Character() != '\n' && Character() != '\r')
	{
		Advance();
	}
}

void FLexer::SkipBlockComment()
{
	const std::size_t Begin = SkipSplices(Position);
	Advance();
	Advance();
	while (Character() != '\0')
	{
		if (Matches('*', '/'))
		{
			Advance();
			Advance();
			return;
		}
		if (Character() == '\n' || Character() == '\r')
		{
			bAtLineStart = true;
		}
		Advance();
	}
	Fail(Begin, "Unterminated block comment.");
}

void FLexer::SkipDirective()
{
	while (Character() != '\0' && Character() != '\r' && Character() != '\n')
	{
		if (Matches('/', '/'))
		{
			SkipLineComment();
		}
		else if (Matches('/', '*'))
		{
			SkipBlockComment();
		}
		else if (IsWhitespace(Character()))
		{
			Advance();
		}
		else
		{
			ReadToken();
		}
	}
}

void FLexer::SkipTrivia()
{
	for (;;)
	{
		if (IsWhitespace(Character()))
		{
			if (Character() == '\n' || Character() == '\r')
			{
				bAtLineStart = true;
			}
			Advance();
		}
		else if (Matches('/', '/'))
		{
			SkipLineComment();
		}
		else if (Matches('/', '*'))
		{
			SkipBlockComment();
		}
		else if (bIgnoreDirectives && bAtLineStart && (Character() == '#' || Matches('%', ':')))
		{
			SkipDirective();
		}
		else
		{
			return;
		}
	}
}

std::string FLexer::ReadIdentifier()
{
	std::string Text;
	while (IsIdentifierContinue(Character()))
	{
		Text.push_back(Character());
		Advance();
	}
	return Text;
}

void FLexer::ReadQuotedLiteral(std::size_t Begin)
{
	const char Quote = Character();
	Advance();
	while (Character() != '\0')
	{
		const char Current = Character();
		if (Current == Quote)
		{
			Advance();
			return;
		}
		if (Current == '\n' || Current == '\r')
		{
			break;
		}
		Advance();
		if (Current == '\\')
		{
			if (Character() == '\0')
			{
				break;
			}
			Advance();
		}
	}
	Fail(Begin, "Unterminated string or character literal.");
}

void FLexer::ReadRawLiteral(std::size_t Begin)
{
	Advance();
	// Raw string bodies retain physical newlines and backslashes.
	const std::size_t DelimiterBegin = Position;
	while (Position < Source.size() && Source[Position] != '(')
	{
		const char Current = Source[Position];
		if (Position - DelimiterBegin >= 16 || IsWhitespace(Current) || Current == '\\' || Current == ')' ||
		    static_cast<unsigned char>(Current) < 0x20)
		{
			Fail(Begin, "Invalid raw string delimiter.");
		}
		++Position;
	}
	if (Position == Source.size())
	{
		Fail(Begin, "Unterminated raw string literal.");
	}
	const std::string Terminator = ")" + Source.substr(DelimiterBegin, Position - DelimiterBegin) + "\"";
	const std::size_t End = Source.find(Terminator, Position + 1);
	if (End == std::string::npos)
	{
		Fail(Begin, "Unterminated raw string literal.");
	}
	Position = End + Terminator.size();
}

void FLexer::ReadNumber()
{
	char Previous = '\0';
	for (;;)
	{
		const char Current = Character();
		if (IsIdentifierContinue(Current) || Current == '.' ||
		    (Current == '\'' && IsIdentifierContinue(Character(1))) ||
		    ((Current == '+' || Current == '-') &&
		     (Previous == 'e' || Previous == 'E' || Previous == 'p' || Previous == 'P')))
		{
			Previous = Current;
			Advance();
		}
		else
		{
			return;
		}
	}
}

FToken FLexer::ReadToken()
{
	FToken Token;
	Token.Begin = SkipSplices(Position);
	if (Character() == '\0')
	{
		Token.End = Token.Begin;
		return Token;
	}
	bAtLineStart = false;
	if (IsIdentifierStart(Character()))
	{
		Token.Text = ReadIdentifier();
		const bool bRawPrefix =
		    Token.Text == "R" || Token.Text == "u8R" || Token.Text == "uR" || Token.Text == "UR" || Token.Text == "LR";
		const bool bQuotedPrefix = Token.Text == "u8" || Token.Text == "u" || Token.Text == "U" || Token.Text == "L";
		if (bRawPrefix && Character() == '"')
		{
			ReadRawLiteral(Token.Begin);
			Token.Kind = ETokenKind::Literal;
		}
		else if (bQuotedPrefix && (Character() == '"' || Character() == '\''))
		{
			ReadQuotedLiteral(Token.Begin);
			Token.Kind = ETokenKind::Literal;
		}
		else
		{
			Token.Kind = ETokenKind::Identifier;
		}
	}
	else if (Character() == '"' || Character() == '\'')
	{
		ReadQuotedLiteral(Token.Begin);
		Token.Kind = ETokenKind::Literal;
	}
	else if (IsDigit(Character()) || (Character() == '.' && IsDigit(Character(1))))
	{
		ReadNumber();
		Token.Kind = ETokenKind::Literal;
	}
	else
	{
		Token.Kind = ETokenKind::Punctuation;
		Token.Punctuation = Character();
		Advance();
	}
	if (Token.Kind == ETokenKind::Literal && IsIdentifierStart(Character()))
	{
		ReadIdentifier();
	}
	Token.End = Position;
	return Token;
}
} // namespace Mod::HeaderTool::Detail
