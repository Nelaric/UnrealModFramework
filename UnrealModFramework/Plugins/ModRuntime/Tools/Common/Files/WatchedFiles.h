// Copyright (c) 2026 Nelaric Contributors
/** @file WatchedFiles.h */
#pragma once

#include <filesystem>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::Tools
{
namespace fs = std::filesystem;

class WatchedFiles
{
	struct PathLess
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

	static bool IsExcludedDirectory(const fs::path& Path);

	static bool IsSourceFile(const fs::path& Path);

public:
	static std::vector<fs::path> CollectSourceFiles(std::vector<fs::path> Pending);

	static std::vector<fs::path> CollectConfigFiles(const fs::path& Root);
};
} // namespace Mod::Tools
