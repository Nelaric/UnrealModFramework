// Copyright (c) 2026 Nelaric Contributors
#include "PathRelations.h"

#include <filesystem>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

bool PathRelations::SamePath(const fs::path& Left, const fs::path& Right)
{
#ifdef _WIN32
	const std::wstring LeftText = Left.lexically_normal().native();
	const std::wstring RightText = Right.lexically_normal().native();
	return CompareStringOrdinal(LeftText.c_str(), -1, RightText.c_str(), -1, TRUE) == CSTR_EQUAL;
#else
	return Left.lexically_normal() == Right.lexically_normal();
#endif
}

bool PathRelations::IsInside(const fs::path& Path, const fs::path& Root)
{
	const fs::path NormalPath = Path.lexically_normal();
	const fs::path NormalRoot = Root.lexically_normal();
	auto Part = NormalPath.begin();
	for (const fs::path& RootPart : NormalRoot)
	{
		if (RootPart.empty())
		{
			continue;
		}
		if (Part == NormalPath.end() || !SamePath(*Part, RootPart))
		{
			return false;
		}
		++Part;
	}
	return true;
}
} // namespace Mod::SdkTool
