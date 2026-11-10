// Copyright (c) 2026 Nelaric Contributors
#include "StageFiles.h"

#include "Common/Files/Path.h"

#include <filesystem>
#include <stdexcept>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

void StageFiles::CopyFile(const fs::path& Source, const fs::path& Destination)
{
	fs::create_directories(Destination.parent_path());
	fs::copy_file(Source, Destination);
}

void StageFiles::CopyTree(const fs::path& Source, const fs::path& Destination, bool bPluginSource)
{
	if (!fs::is_directory(Source))
	{
		throw std::runtime_error("Missing package directory: " + Mod::BuildTool::Path::PathToUtf8(Source));
	}
	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(Source))
	{
		if (Entry.is_symlink())
		{
			throw std::runtime_error("SDK packages cannot contain symbolic links.");
		}
		const fs::path Relative = Entry.path().lexically_relative(Source);
		if (Entry.is_regular_file() &&
		    (!bPluginSource || (*Relative.begin() != "Binaries" && *Relative.begin() != "Intermediate")))
		{
			CopyFile(Entry.path(), Destination / Relative);
		}
	}
}
} // namespace Mod::SdkTool
