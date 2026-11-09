// Copyright (c) 2026 Nelaric Contributors
/** @file WatchedFiles.h */
#pragma once

#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::Tools
{
namespace fs = std::filesystem;

inline std::u8string LowercaseName(const fs::path& Path)
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

struct FPathLess
{
	bool operator()(const fs::path& Left, const fs::path& Right) const
	{
#ifdef _WIN32
		const int Comparison = CompareStringOrdinal(Left.c_str(), -1, Right.c_str(), -1, TRUE);
		if (Comparison == 0)
		{
			throw std::runtime_error("Failed to compare Windows paths.");
		}
		return Comparison == CSTR_LESS_THAN;
#else
		return Left < Right;
#endif
	}
};

inline bool IsExcludedDirectory(const fs::path& Path)
{
	const auto Name = LowercaseName(Path.filename());
	return Name == u8"binaries" || Name == u8"intermediate" || Name == u8"saved" || Name == u8"deriveddatacache" ||
	       Name == u8"build" || Name == u8"cmakefiles" || Name == u8".git" || Name == u8".vs" || Name == u8".idea" ||
	       Name == u8".vscode" || Name == u8".tools" || Name.starts_with(u8"cmake-build-");
}

inline bool IsSourceFile(const fs::path& Path)
{
	const auto Name = LowercaseName(Path.filename());
	if (Name.ends_with(u8".generated.h") || Name.ends_with(u8".gen.cpp") || Name.ends_with(u8".generated.cpp"))
	{
		return false;
	}
	const auto Extension = LowercaseName(Path.extension());
	return Extension == u8".h" || Extension == u8".hh" || Extension == u8".hpp" || Extension == u8".hxx" ||
	       Extension == u8".inl" || Extension == u8".ipp" || Extension == u8".tpp" || Extension == u8".c" ||
	       Extension == u8".cc" || Extension == u8".cpp" || Extension == u8".cxx" || Extension == u8".ixx" ||
	       Extension == u8".cppm";
}

inline std::vector<fs::path> CollectSourceFiles(std::vector<fs::path> Pending)
{
	std::set<fs::path, FPathLess> VisitedDirectories;
	std::set<fs::path, FPathLess> Files;
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

inline std::vector<fs::path> CollectConfigFiles(const fs::path& Root)
{
	std::vector<fs::path> Files;
	if (fs::is_directory(Root))
	{
		for (const auto& Entry : fs::recursive_directory_iterator(Root))
		{
			if (Entry.is_regular_file() && LowercaseName(Entry.path().extension()) == u8".ini")
			{
				Files.push_back(fs::weakly_canonical(Entry.path()));
			}
		}
	}
	return Files;
}
} // namespace Mod::Tools
