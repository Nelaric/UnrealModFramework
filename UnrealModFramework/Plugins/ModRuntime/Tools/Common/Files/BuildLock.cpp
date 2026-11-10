// Copyright (c) 2026 Nelaric Contributors
#include "BuildLock.h"

#include "Common/Files/Path.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

BuildLock::BuildLock(const fs::path& Output) : BuildLock(Output, ".mbt-lock")
{
}

BuildLock::BuildLock(const fs::path& Output, std::string_view Suffix)
{
	fs::create_directories(Output.parent_path());
	fs::path LockPath = Output;
	LockPath += Suffix;
#ifdef _WIN32
	const HANDLE File = CreateFileW(LockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
	                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (File == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("Cannot lock output (possibly in use): " + Path::PathToUtf8(Output));
	}
	Handle = reinterpret_cast<std::intptr_t>(File);
#else
	const int File = open(LockPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (File < 0 || flock(File, LOCK_EX | LOCK_NB) != 0)
	{
		if (File >= 0)
		{
			close(File);
		}
		throw std::runtime_error("Cannot lock output (possibly in use): " + Path::PathToUtf8(Output));
	}
	Handle = File;
#endif
}

BuildLock::~BuildLock()
{
#ifdef _WIN32
	if (Handle != -1)
	{
		CloseHandle(reinterpret_cast<HANDLE>(Handle));
	}
#else
	if (Handle >= 0)
	{
		close(static_cast<int>(Handle));
	}
#endif
}
} // namespace Mod::BuildTool
