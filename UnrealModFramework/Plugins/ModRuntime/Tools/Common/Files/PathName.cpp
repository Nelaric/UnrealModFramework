// Copyright (c) 2026 Nelaric Contributors
#include "PathName.h"

#include <filesystem>
#include <string>

namespace Mod::Tools
{
namespace fs = std::filesystem;

std::u8string PathName::LowercaseName(const fs::path& Path)
{
	auto Name = Path.u8string();
	for (auto& Character : Name)
	{
		if (Character >= u8'A' && Character <= u8'Z')
		{
			Character += u8'a' - u8'A';
		}
	}
	return Name;
}
} // namespace Mod::Tools
