// Copyright (c) 2026 Nelaric Contributors
/** @file ValueLayout.h */
#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Mod::Tools
{
struct ValueField
{
	std::string Name;
	std::string_view Scalar;
	std::vector<ValueField> Children;
};

class ValueLayout
{
public:
	static std::vector<ValueField> Parse(std::string_view Text)
	{
		std::size_t Cursor = 0;
		unsigned Count = 0;
		auto Fields = ReadFields(Text, Cursor, 0, Count);
		if (Cursor != Text.size())
		{
			throw std::runtime_error("Trailing extension struct data.");
		}
		return Fields;
	}

private:
	static std::vector<ValueField> ReadFields(std::string_view Text, std::size_t& Cursor, unsigned Depth,
	                                          unsigned& Count)
	{
		if (Depth >= 8 || Cursor >= Text.size() || Text[Cursor++] != '{')
		{
			throw std::runtime_error("Invalid extension struct boundary.");
		}
		std::vector<ValueField> Result;
		while (Cursor < Text.size() && Text[Cursor] != '}')
		{
			const auto Colon = Text.find(':', Cursor);
			const auto Name = Text.substr(Cursor, Colon - Cursor);
			constexpr std::string_view Letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_";
			constexpr std::string_view Identifier = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";
			if (++Count > 128 || Colon == Text.npos || Name.empty() || Letters.find(Name.front()) == Letters.npos ||
			    Name.find_first_not_of(Identifier) != Name.npos ||
			    std::ranges::any_of(Result, [Name](const auto& Field) { return Field.Name == Name; }))
			{
				throw std::runtime_error("Invalid extension field name: " + std::string(Name));
			}
			ValueField Field{std::string(Name), {}, {}};
			Cursor = Colon + 1;
			if (Cursor < Text.size() && Text[Cursor] == '{')
			{
				Field.Children = ReadFields(Text, Cursor, Depth + 1, Count);
			}
			else
			{
				const auto End = Text.find(';', Cursor);
				Field.Scalar = Text.substr(Cursor, End - Cursor);
				if (End == Text.npos || (Field.Scalar != "FloatProperty" && Field.Scalar != "DoubleProperty" &&
				                         Field.Scalar != "IntProperty" && Field.Scalar != "Int64Property" &&
				                         Field.Scalar != "BoolProperty" && Field.Scalar != "ByteProperty"))
				{
					throw std::runtime_error("Invalid extension field type: " + Field.Name);
				}
				Cursor = End;
			}
			if (Cursor >= Text.size() || Text[Cursor++] != ';')
			{
				throw std::runtime_error("Invalid extension field separator.");
			}
			Result.push_back(std::move(Field));
		}
		if (Result.empty() || Cursor >= Text.size() || Text[Cursor++] != '}')
		{
			throw std::runtime_error("Invalid extension struct boundary.");
		}
		return Result;
	}
};
} // namespace Mod::Tools
