// Copyright (c) 2026 Nelaric Contributors
#include "OwnedStage.h"

#include <filesystem>
#include <system_error>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

OwnedStage::~OwnedStage()
{
	std::error_code Ignored;
	const fs::path Resolved = fs::weakly_canonical(Path, Ignored);
	if (!Ignored && Path.is_absolute() && Path.parent_path() == Parent && Resolved.parent_path() == Parent &&
	    Path.filename().string().starts_with(".mbt-stage-"))
	{
		fs::remove_all(Path, Ignored);
	}
}
} // namespace Mod::BuildTool
