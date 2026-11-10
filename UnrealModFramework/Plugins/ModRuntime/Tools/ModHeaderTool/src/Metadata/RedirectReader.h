// Copyright (c) 2026 Nelaric Contributors
/** @file RedirectReader.h */
#pragma once

#include "ModHeaderTool/src/Metadata/RedirectFunctionName.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
using Json = nlohmann::json;

class RedirectReader
{
public:
	static RedirectFunctionName RedirectName(const Json& Name);
	static std::string RedirectLocation(std::string_view Location);
};
} // namespace Mod::HeaderTool
