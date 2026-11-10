// Copyright (c) 2026 Nelaric Contributors
/** @file ListingText.h */
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Mod::SourceTool
{
class ListingText
{
public:
	static std::string_view Trim(std::string_view Text);
	static std::pair<std::string_view, std::string_view> Split(std::string_view Text, std::size_t Separator);
	static std::optional<std::uint64_t> Unsigned(std::string_view Text);
	static std::string_view Word(std::string_view& Text);
	static unsigned Number(std::string_view& Text);
	static std::string Quoted(std::string_view& Text);
	static std::string_view NextLine(std::string_view& Text);
};
} // namespace Mod::SourceTool
