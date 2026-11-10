// Copyright (c) 2026 Nelaric Contributors
#include "MetadataName.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace Mod::HeaderTool
{
bool MetadataName::IsCompatibleFunctionName(std::string_view Name)
{
	const auto IsLetter = [](char Character)
	{ return (Character >= 'a' && Character <= 'z') || (Character >= 'A' && Character <= 'Z'); };
	if (Name.empty() || (!IsLetter(Name.front()) && Name.front() != '_') ||
	    !std::all_of(Name.begin(), Name.end(), [&](char Character)
	                 { return IsLetter(Character) || (Character >= '0' && Character <= '9') || Character == '_'; }))
	{
		return false;
	}
	constexpr std::string_view Keywords[] = {
	    "alignas",     "alignof",  "and",       "and_eq",    "asm",       "auto",         "bitand",
	    "bitor",       "bool",     "break",     "case",      "catch",     "char",         "char8_t",
	    "char16_t",    "char32_t", "class",     "co_await",  "co_return", "co_yield",     "compl",
	    "concept",     "const",    "consteval", "constexpr", "constinit", "const_cast",   "continue",
	    "decltype",    "default",  "delete",    "do",        "double",    "dynamic_cast", "else",
	    "enum",        "explicit", "export",    "extern",    "false",     "float",        "for",
	    "friend",      "goto",     "if",        "inline",    "int",       "long",         "mutable",
	    "namespace",   "new",      "noexcept",  "not",       "not_eq",    "nullptr",      "operator",
	    "or",          "or_eq",    "private",   "protected", "public",    "register",     "reinterpret_cast",
	    "requires",    "return",   "short",     "signed",    "sizeof",    "static",       "static_assert",
	    "static_cast", "struct",   "switch",    "template",  "this",      "thread_local", "throw",
	    "true",        "try",      "typedef",   "typeid",    "typename",  "union",        "unsigned",
	    "using",       "virtual",  "void",      "volatile",  "wchar_t",   "while",        "xor",
	    "xor_eq"};
	return std::find(std::begin(Keywords), std::end(Keywords), Name) == std::end(Keywords);
}
} // namespace Mod::HeaderTool
