// Copyright (c) 2026 Nelaric Contributors
/** @file CatalogText.h */
#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

class CatalogText
{
public:
	static std::string Text(const Json& Object, const char* Key);
};
} // namespace Mod::SourceTool
