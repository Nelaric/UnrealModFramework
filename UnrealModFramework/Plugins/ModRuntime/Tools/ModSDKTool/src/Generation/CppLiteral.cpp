// Copyright (c) 2026 Nelaric Contributors
#include "CppLiteral.h"

#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

std::string CppLiteral::Literal(const std::string& Value)
{
	return Json(Value).dump();
}
} // namespace Mod::SdkTool
