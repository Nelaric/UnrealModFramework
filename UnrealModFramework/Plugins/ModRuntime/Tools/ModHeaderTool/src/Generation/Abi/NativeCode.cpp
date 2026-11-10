// Copyright (c) 2026 Nelaric Contributors
#include "NativeCode.h"

#include <cctype>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

std::string NativeCode::Literal(const std::string& Value)
{
	return Json(Value).dump();
}

std::string NativeCode::Upper(std::string Value)
{
	for (char& C : Value)
	{
		C = static_cast<char>(std::toupper(static_cast<unsigned char>(C)));
	}
	return Value;
}

std::string NativeCode::Source(const std::string& File)
{
	return "// Copyright (c) 2026 Nelaric Contributors\n/** @file " + File + " */\n";
}
} // namespace Mod::HeaderTool
