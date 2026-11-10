// Copyright (c) 2026 Nelaric Contributors
#include "ListingText.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Mod::SourceTool
{
std::string_view ListingText::Trim(std::string_view Text)
{
	const auto Start = Text.find_first_not_of(" \t\r");
	return Start == Text.npos ? std::string_view{} : Text.substr(Start, Text.find_last_not_of(" \t\r") - Start + 1);
}

std::pair<std::string_view, std::string_view> ListingText::Split(std::string_view Text, std::size_t Separator)
{
	return {Trim(Text.substr(0, Separator)),
	        Separator == Text.npos ? std::string_view{} : Trim(Text.substr(Separator + 1))};
}

std::optional<std::uint64_t> ListingText::Unsigned(std::string_view Text)
{
	const bool bSuffix = Text.ends_with('H') || Text.ends_with('h');
	const bool bPrefix = Text.starts_with("0x") || Text.starts_with("0X");
	if (bSuffix)
	{
		Text.remove_suffix(1);
	}
	if (bPrefix)
	{
		Text.remove_prefix(2);
	}
	if (Text.empty())
	{
		return std::nullopt;
	}
	std::uint64_t Value{};
	const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Value, bSuffix || bPrefix ? 16 : 10);
	return Result.ec == std::errc{} && Result.ptr == Text.data() + Text.size() ? std::optional{Value} : std::nullopt;
}

std::string_view ListingText::Word(std::string_view& Text)
{
	Text = Trim(Text);
	const auto End = Text.find_first_of(" \t");
	const auto Result = Text.substr(0, End);
	Text.remove_prefix(End == Text.npos ? Text.size() : End);
	return Result;
}

unsigned ListingText::Number(std::string_view& Text)
{
	return static_cast<unsigned>(Unsigned(Word(Text)).value_or(0));
}

std::string ListingText::Quoted(std::string_view& Text)
{
	Text = Trim(Text);
	std::string Result;
	if (!Text.starts_with('"'))
	{
		return Result;
	}
	Text.remove_prefix(1);
	while (!Text.empty())
	{
		char Ch = Text.front();
		Text.remove_prefix(1);
		if (Ch == '"')
		{
			break;
		}
		if (Ch == '\\' && !Text.empty())
		{
			Ch = Text.front();
			Text.remove_prefix(1);
		}
		Result += Ch;
	}
	return Result;
}

std::string_view ListingText::NextLine(std::string_view& Text)
{
	const auto End = Text.find('\n');
	const auto Line = Trim(Text.substr(0, End));
	Text.remove_prefix(End == Text.npos ? Text.size() : End + 1);
	return Line;
}
} // namespace Mod::SourceTool
