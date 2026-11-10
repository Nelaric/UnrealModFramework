// Copyright (c) 2026 Nelaric Contributors
#include "SourceCharacter.h"

namespace Mod::HeaderTool::Detail
{
bool SourceCharacter::IsIdentifierStart(char Character)
{
	return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z') || Character == '_' ||
	       static_cast<unsigned char>(Character) >= 0x80;
}

bool SourceCharacter::IsDigit(char Character)
{
	return Character >= '0' && Character <= '9';
}

bool SourceCharacter::IsIdentifierContinue(char Character)
{
	return IsIdentifierStart(Character) || IsDigit(Character);
}

bool SourceCharacter::IsWhitespace(char Character)
{
	return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n' || Character == '\v' ||
	       Character == '\f';
}
} // namespace Mod::HeaderTool::Detail
