// Copyright (c) 2026 Nelaric Contributors
/** @file BuildLock.h */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

/// Holds an OS lock until destruction, including abnormal process exit.
class BuildLock
{
public:
	/// Fails immediately if another writer owns this output's lock.
	explicit BuildLock(const fs::path& Output);
	/// Uses a lock path with the given suffix.
	BuildLock(const fs::path& Output, std::string_view Suffix);
	/// Releases the operating-system lock.
	~BuildLock();
	BuildLock(const BuildLock&) = delete;
	BuildLock& operator=(const BuildLock&) = delete;

private:
	std::intptr_t Handle = -1;
};
} // namespace Mod::BuildTool
