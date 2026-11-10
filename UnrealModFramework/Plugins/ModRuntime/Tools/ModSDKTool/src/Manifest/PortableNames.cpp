// Copyright (c) 2026 Nelaric Contributors
#include "PortableNames.h"

#include "Common/Files/Path.h"

#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

std::string PortableNames::Fold(std::string Value)
{
	for (char& Character : Value)
	{
		Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
	}
	return Value;
}

void PortableNames::Identifier(std::string_view Value)
{
	static const std::set<std::string_view> Reserved = {
	    "alignas",     "alignof",  "asm",      "auto",         "bool",      "break",    "case",      "catch",
	    "char",        "class",    "concept",  "const",        "constexpr", "continue", "decltype",  "default",
	    "delete",      "do",       "double",   "else",         "enum",      "explicit", "export",    "extern",
	    "false",       "float",    "for",      "friend",       "if",        "inline",   "int",       "long",
	    "namespace",   "new",      "noexcept", "nullptr",      "operator",  "private",  "protected", "public",
	    "register",    "requires", "return",   "short",        "signed",    "sizeof",   "static",    "struct",
	    "switch",      "template", "this",     "thread_local", "throw",     "true",     "try",       "typedef",
	    "typename",    "union",    "unsigned", "using",        "virtual",   "void",     "volatile",  "while",
	    "StaticClass", "GetClass", "GetWorld", "Serialize"};
	if (Value.empty() || Value.size() > 80 || !std::isalpha(static_cast<unsigned char>(Value.front())) ||
	    Value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
	        std::string_view::npos ||
	    Reserved.contains(Value))
	{
		throw std::runtime_error("Invalid or reserved generated identifier: " + std::string(Value));
	}
}

void PortableNames::PortablePath(std::string_view Value)
{
	if (Value.empty() || Value.find_first_of("\\:\r\n\"<>|?*") != std::string_view::npos)
	{
		throw std::runtime_error("Expected a portable relative public path.");
	}
	const fs::path Path = Mod::BuildTool::Path::PathFromUtf8(Value);
	if (Path.is_absolute() || Path.has_root_name())
	{
		throw std::runtime_error("Public include paths must be relative.");
	}
	for (const fs::path& Part : Path)
	{
		const std::string Name = Mod::BuildTool::Path::PathToUtf8(Part);
		if (Name.empty() || Name == "." || Name == ".." || Name.back() == '.' || Name.back() == ' ')
		{
			throw std::runtime_error("Public paths cannot contain traversal or ambiguous components.");
		}
	}
}
} // namespace Mod::SdkTool
