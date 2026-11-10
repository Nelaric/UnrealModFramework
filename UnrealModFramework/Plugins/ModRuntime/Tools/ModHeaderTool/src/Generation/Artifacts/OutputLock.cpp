// Copyright (c) 2026 Nelaric Contributors
#include "OutputLock.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"
#include "ModHeaderTool/src/Generation/Artifacts/OutputPaths.h"

#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

OutputLock::OutputLock(const fs::path& Root)
{
	const fs::path File = OutputPaths::CheckedPath(Root, ".mht.lock");
#ifdef _WIN32
	Handle = CreateFileW(File.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
	                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (Handle == INVALID_HANDLE_VALUE)
#else
	Handle = open(File.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (Handle < 0 || flock(Handle, LOCK_EX | LOCK_NB) != 0)
#endif
	{
#ifndef _WIN32
		if (Handle >= 0)
		{
			close(Handle);
		}
#endif
		throw std::runtime_error("Cannot lock MHT output directory: " + DiagnosticLocation::QuoteDiagnosticPath(Root));
	}
}

OutputLock::~OutputLock()
{
#ifdef _WIN32
	CloseHandle(Handle);
#else
	close(Handle);
#endif
}
} // namespace Mod::HeaderTool
