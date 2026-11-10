// Copyright (c) 2026 Nelaric Contributors
#include "Path.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

std::string Path::PathToUtf8(const fs::path& Path)
{
	const std::u8string Text = Path.generic_u8string();
	return std::string(Text.begin(), Text.end());
}

fs::path Path::PathFromUtf8(std::string_view Text)
{
	std::u8string Utf8;
	Utf8.reserve(Text.size());
	for (unsigned char Character : Text)
	{
		Utf8.push_back(static_cast<char8_t>(Character));
	}
	return fs::path(Utf8);
}
} // namespace Mod::HeaderTool
