// Copyright (c) 2026 Nelaric Contributors
#include "ProcessArguments.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
std::string ProcessArguments::QuoteMsvc(std::string_view Value)
{
	std::string Result = "\"";
	std::size_t Slashes = 0;
	for (char Character : Value)
	{
		if (Character == '\\')
		{
			++Slashes;
			continue;
		}
		Result.append(Slashes * (Character == '"' ? 2 : 1), '\\');
		Slashes = 0;
		if (Character == '"')
		{
			Result.push_back('\\');
		}
		Result.push_back(Character);
	}
	Result.append(Slashes * 2, '\\');
	Result.push_back('"');
	return Result;
}

void ProcessArguments::RequireArgument(std::string_view Value)
{
	if (Value.find_first_of("\r\n") != std::string_view::npos || Value.find('\0') != std::string_view::npos)
	{
		throw std::runtime_error("Tool arguments cannot contain NUL or newline characters.");
	}
}
} // namespace Mod::BuildTool
