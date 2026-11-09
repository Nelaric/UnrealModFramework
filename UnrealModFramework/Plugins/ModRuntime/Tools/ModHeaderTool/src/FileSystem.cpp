// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/FileSystem.h"

#include <fstream>
#include <iterator>
#include <stdexcept>

namespace Mod::HeaderTool
{
std::string PathToUtf8(const fs::path& Path)
{
	const std::u8string Text = Path.generic_u8string();
	return std::string(Text.begin(), Text.end());
}

fs::path PathFromUtf8(std::string_view Text)
{
	std::u8string Utf8;
	Utf8.reserve(Text.size());
	for (unsigned char Character : Text)
	{
		Utf8.push_back(static_cast<char8_t>(Character));
	}
	return fs::path(Utf8);
}

std::string ReadTextFile(const fs::path& Path)
{
	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		throw std::runtime_error("Cannot open file: " + PathToUtf8(Path));
	}
	std::string Contents((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	if (File.bad())
	{
		throw std::runtime_error("Cannot read file: " + PathToUtf8(Path));
	}
	return Contents;
}
} // namespace Mod::HeaderTool
