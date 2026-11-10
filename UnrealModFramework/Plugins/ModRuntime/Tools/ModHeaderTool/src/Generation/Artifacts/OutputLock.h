// Copyright (c) 2026 Nelaric Contributors
/** @file OutputLock.h */
#pragma once

#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class OutputLock
{
public:
	explicit OutputLock(const fs::path& Root);
	~OutputLock();
	OutputLock(const OutputLock&) = delete;
	OutputLock& operator=(const OutputLock&) = delete;

private:
#ifdef _WIN32
	HANDLE Handle = INVALID_HANDLE_VALUE;
#else
	int Handle = -1;
#endif
};
} // namespace Mod::HeaderTool
