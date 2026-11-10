// Copyright (c) 2026 Nelaric Contributors
/** @file SourceCharacter.h */
#pragma once

namespace Mod::HeaderTool::Detail
{
class SourceCharacter
{
public:
	static bool IsIdentifierStart(char Character);
	static bool IsDigit(char Character);
	static bool IsIdentifierContinue(char Character);
	static bool IsWhitespace(char Character);
};
} // namespace Mod::HeaderTool::Detail
