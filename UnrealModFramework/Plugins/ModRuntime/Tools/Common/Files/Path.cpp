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

fs::path Path::FileSystemPath(const fs::path& Path)
{
#ifdef _WIN32
	if (!Path.empty())
	{
		const fs::path Absolute = fs::absolute(Path).lexically_normal().make_preferred();
		const std::wstring& Native = Absolute.native();
		if (Native.starts_with(LR"(\\?\)") || Native.starts_with(LR"(\\.\)"))
		{
			return Absolute;
		}
		if (Native.starts_with(LR"(\\)"))
		{
			return fs::path(LR"(\\?\UNC\)" + Native.substr(2));
		}
		return fs::path(LR"(\\?\)" + Native);
	}
#endif
	return Path;
}

fs::path Path::ResolvePath(const fs::path& Base, std::string_view Text)
{
	if (Text.empty())
	{
		throw std::runtime_error("A required path is empty.");
	}
	return fs::weakly_canonical(FileSystemPath(fs::absolute(Base / PathFromUtf8(Text))));
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
	const fs::path Inside = FileSystemPath(Resolved).lexically_relative(FileSystemPath(Root));
	if (Inside.empty() || Inside == "." || *Inside.begin() == "..")
	{
		throw std::runtime_error("Generated path escapes its root: " + std::string(Text));
	}
	return Resolved;
}
} // namespace Mod::BuildTool
