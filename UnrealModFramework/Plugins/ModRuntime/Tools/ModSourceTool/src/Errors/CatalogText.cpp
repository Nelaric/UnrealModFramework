// Copyright (c) 2026 Nelaric Contributors
#include "CatalogText.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::SourceTool
{
using Json = nlohmann::json;

std::string CatalogText::Text(const Json& Object, const char* Key)
{
	const auto Value = Object.at(Key).get<std::string>();
	if (Value.empty())
	{
		throw std::runtime_error(std::string("Empty catalog field: ") + Key);
	}
	return Value;
}
} // namespace Mod::SourceTool
