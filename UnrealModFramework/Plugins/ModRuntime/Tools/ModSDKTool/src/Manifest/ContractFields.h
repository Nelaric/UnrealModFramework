// Copyright (c) 2026 Nelaric Contributors
/** @file ContractFields.h */
#pragma once

#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
using Json = nlohmann::json;

class ContractFields
{
public:
	static const char* AdapterABIKey(const Json& Document);
};
} // namespace Mod::SdkTool
