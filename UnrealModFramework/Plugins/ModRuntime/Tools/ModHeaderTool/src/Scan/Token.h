// Copyright (c) 2026 Nelaric Contributors
/** @file Token.h */
#pragma once

#include <cstddef>
#include <string>

namespace Mod::HeaderTool::Detail
{
struct Token
{
	enum class EKind
	{
		End,
		Identifier,
		Literal,
		Punctuation,
	};

	EKind Kind = EKind::End;
	std::string Text;
	char Punctuation = '\0';
	std::size_t Begin = 0;
	std::size_t End = 0;
};
} // namespace Mod::HeaderTool::Detail
