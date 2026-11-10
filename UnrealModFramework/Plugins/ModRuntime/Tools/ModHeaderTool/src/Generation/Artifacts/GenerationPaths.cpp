// Copyright (c) 2026 Nelaric Contributors
#include "GenerationPaths.h"

#include "ModHeaderTool/src/Files/Path.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Mod::HeaderTool
{
std::string GenerationPaths::NormalizeInput(const GenerationInput& Input, std::string Text)
{
	std::replace(Text.begin(), Text.end(), '\\', '/');
	std::vector<std::pair<std::string, std::string>> Roots;
	for (const auto& [Name, Path] : Input.PathRoots)
	{
		Roots.emplace_back(Path::PathToUtf8(Path), "<" + Name + ">");
	}
	std::sort(Roots.begin(), Roots.end(), [](const auto& A, const auto& B) { return A.first.size() > B.first.size(); });
	for (const auto& [Path, Name] : Roots)
	{
		std::size_t Position = 0;
		while ((Position = Text.find(Path, Position)) != Text.npos)
		{
			Text.replace(Position, Path.size(), Name);
			Position += Name.size();
		}
	}
	return Text;
}
} // namespace Mod::HeaderTool
