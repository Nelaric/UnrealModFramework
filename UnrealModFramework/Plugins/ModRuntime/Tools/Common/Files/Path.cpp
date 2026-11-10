// Copyright (c) 2026 Nelaric Contributors
#include "Path.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

std::string Path::PathToUtf8(const fs::path& Path)
{
	const std::u8string Text = Path.generic_u8string();
	return {Text.begin(), Text.end()};
}

fs::path Path::PathFromUtf8(std::string_view Text)
{
	if (Text.find('\0') != std::string_view::npos)
	{
		throw std::runtime_error("A path contains a NUL character.");
	}
	std::u8string Utf8;
	for (unsigned char Character : Text)
	{
		Utf8.push_back(static_cast<char8_t>(Character));
	}
	return fs::path(Utf8);
}

fs::path Path::ResolvePath(const fs::path& Base, std::string_view Text)
{
	if (Text.empty())
	{
		throw std::runtime_error("A required path is empty.");
	}
	return fs::weakly_canonical(fs::absolute(Base / PathFromUtf8(Text)));
}

fs::path Path::ResolveGeneratedPath(const fs::path& Root, std::string_view Text)
{
	const fs::path Relative = PathFromUtf8(Text);
	if (Relative.empty() || Relative.is_absolute() || Relative.has_root_name())
	{
		throw std::runtime_error("Generated paths must be relative: " + std::string(Text));
	}
	for (const fs::path& Part : Relative)
	{
		if (Part == "..")
		{
			throw std::runtime_error("Generated paths cannot contain '..': " + std::string(Text));
		}
	}
	const fs::path Resolved = ResolvePath(Root, Text);
	const fs::path Inside = Resolved.lexically_relative(Root);
	if (Inside.empty() || *Inside.begin() == ".." || Resolved == Root)
	{
		throw std::runtime_error("Generated path escapes its root: " + std::string(Text));
	}
	return Resolved;
}
} // namespace Mod::BuildTool
