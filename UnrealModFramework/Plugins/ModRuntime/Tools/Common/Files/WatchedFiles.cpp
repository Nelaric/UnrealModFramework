// Copyright (c) 2026 Nelaric Contributors
#include "WatchedFiles.h"

#include "Common/Files/PathName.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <vector>

namespace Mod::Tools
{
namespace fs = std::filesystem;

bool WatchedFiles::IsExcludedDirectory(const fs::path& Path)
{
	const auto Name = PathName::LowercaseName(Path.filename());
	return Name == u8"binaries" || Name == u8"intermediate" || Name == u8"saved" || Name == u8"deriveddatacache" ||
	       Name == u8"build" || Name == u8"cmakefiles" || Name == u8".git" || Name == u8".vs" || Name == u8".idea" ||
	       Name == u8".vscode" || Name == u8".tools" || Name.starts_with(u8"cmake-build-");
}

bool WatchedFiles::IsSourceFile(const fs::path& Path)
{
	const auto Name = PathName::LowercaseName(Path.filename());
	if (Name.ends_with(u8".generated.h") || Name.ends_with(u8".gen.cpp") || Name.ends_with(u8".generated.cpp"))
	{
		return false;
	}
	const auto Extension = PathName::LowercaseName(Path.extension());
	return Extension == u8".h" || Extension == u8".hh" || Extension == u8".hpp" || Extension == u8".hxx" ||
	       Extension == u8".inl" || Extension == u8".ipp" || Extension == u8".tpp" || Extension == u8".c" ||
	       Extension == u8".cc" || Extension == u8".cpp" || Extension == u8".cxx" || Extension == u8".ixx" ||
	       Extension == u8".cppm";
}

std::vector<fs::path> WatchedFiles::CollectSourceFiles(std::vector<fs::path> Pending)
{
	std::set<fs::path, PathLess> VisitedDirectories;
	std::set<fs::path, PathLess> Files;
	while (!Pending.empty())
	{
		const fs::path Directory = fs::canonical(Pending.back());
		Pending.pop_back();
		if (!VisitedDirectories.insert(Directory).second)
		{
			continue;
		}
		for (const auto& Entry : fs::directory_iterator(Directory))
		{
			if (Entry.is_directory() && !IsExcludedDirectory(Entry.path()))
			{
				Pending.push_back(Entry.path());
			}
			else if (Entry.is_regular_file() && IsSourceFile(Entry.path()))
			{
				Files.insert(fs::canonical(Entry.path()));
			}
		}
	}
	std::vector<fs::path> Result(Files.begin(), Files.end());
	std::sort(Result.begin(), Result.end(), [](const fs::path& Left, const fs::path& Right)
	          { return Left.generic_u8string() < Right.generic_u8string(); });
	return Result;
}

std::vector<fs::path> WatchedFiles::CollectConfigFiles(const fs::path& Root)
{
	std::vector<fs::path> Files;
	if (fs::is_directory(Root))
	{
		for (const auto& Entry : fs::recursive_directory_iterator(Root))
		{
			if (Entry.is_regular_file() && PathName::LowercaseName(Entry.path().extension()) == u8".ini")
			{
				Files.push_back(fs::weakly_canonical(Entry.path()));
			}
		}
	}
	return Files;
}
} // namespace Mod::Tools
