// Copyright (c) 2026 Nelaric Contributors
#include "GenerationFields.h"

#include "ModHeaderTool/src/Files/Path.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string GenerationFields::Required(const Json& Object, const char* Key)
{
	if (!Object.contains(Key) || !Object[Key].is_string() || Object[Key].get<std::string>().empty() ||
	    Object[Key].get<std::string>().find('\0') != std::string::npos)
	{
		throw std::runtime_error(std::string("Build input requires a nonempty string '") + Key + "'.");
	}
	return Object[Key].get<std::string>();
}

bool GenerationFields::Identifier(std::string_view Text)
{
	return !Text.empty() && (std::isalpha(static_cast<unsigned char>(Text[0])) || Text[0] == '_') &&
	       std::all_of(Text.begin(), Text.end(), [](unsigned char C) { return std::isalnum(C) || C == '_'; });
}

void GenerationFields::RequireName(const std::string& Text)
{
	if (!Identifier(Text))
	{
		throw std::runtime_error("Expected an ASCII identifier in build input, found '" + Text + "'.");
	}
}

std::string GenerationFields::Fold(std::string Text)
{
	std::transform(Text.begin(), Text.end(), Text.begin(), [](unsigned char C) { return std::tolower(C); });
	return Text;
}

fs::path GenerationFields::Resolve(const fs::path& Root, const std::string& Text)
{
	const fs::path Path = Path::PathFromUtf8(Text);
	return fs::canonical(Path.is_absolute() ? Path : Root / Path);
}
} // namespace Mod::HeaderTool
