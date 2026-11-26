// Copyright (c) 2026 Nelaric Contributors
#include "ModuleInput.h"

#include "ModHeaderTool/src/Files/Path.h"

namespace Mod::HeaderTool
{
std::string ModuleInput::HeaderInclude(const fs::path& File) const
{
	const auto Relative = File.lexically_relative(Root);
	const auto First = Relative.begin();
	if (First != Relative.end() &&
	    (*First == "Public" || *First == "Private" || *First == "Internal" || *First == "Classes"))
	{
		return Path::PathToUtf8(Relative.lexically_relative(*First));
	}
	return Path::PathToUtf8(Relative);
}
} // namespace Mod::HeaderTool
