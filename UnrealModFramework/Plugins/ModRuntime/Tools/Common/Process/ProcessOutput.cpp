// Copyright (c) 2026 Nelaric Contributors
#include "ProcessOutput.h"

#include "Common/Files/Path.h"

#include <filesystem>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

#ifdef _WIN32
ProcessOutput::ProcessOutput(const fs::path& File)
{
	if (!File.empty())
	{
		SECURITY_ATTRIBUTES Security{sizeof(Security), nullptr, TRUE};
		Output.Value = CreateFileW(File.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &Security, CREATE_NEW,
		                           FILE_ATTRIBUTE_NORMAL, nullptr);
		if (Output.Value == INVALID_HANDLE_VALUE)
		{
			const auto Error = GetLastError();
			throw std::runtime_error("Cannot create tool result file: " + Path::PathToUtf8(File) + " (Windows error " +
			                         std::to_string(Error) + ").");
		}
	}
}

HANDLE ProcessOutput::GetHandle(HANDLE Diagnostics) const
{
	return Output.Value == nullptr ? Diagnostics : Output.Value;
}
#else
ProcessOutput::ProcessOutput(const fs::path& File) : Output(File.empty() ? fs::path{} : fs::absolute(File))
{
}

bool ProcessOutput::RedirectInChild() const
{
	if (Output.empty())
	{
		return dup2(STDERR_FILENO, STDOUT_FILENO) >= 0;
	}
	const int File = open(Output.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	if (File < 0)
	{
		return false;
	}
	const bool bRedirected = dup2(File, STDOUT_FILENO) >= 0;
	if (File != STDOUT_FILENO)
	{
		close(File);
	}
	return bRedirected;
}
#endif
} // namespace Mod::BuildTool
