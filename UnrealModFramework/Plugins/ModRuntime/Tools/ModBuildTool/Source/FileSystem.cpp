// Copyright (c) 2026 Nelaric Contributors
#include "FileSystem.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

namespace Mod::BuildTool
{
std::string PathToUtf8(const fs::path& Path)
{
	const std::u8string Text = Path.generic_u8string();
	return {Text.begin(), Text.end()};
}

fs::path PathFromUtf8(std::string_view Text)
{
	if (Text.find('\0') != std::string_view::npos)
	{
		throw std::runtime_error("A path contains a NUL character.");
	}
	std::u8string Utf8;
	for (unsigned char Character : Text)
	{
		Utf8.push_back(static_cast<char8_t>(Character));
	}
	return fs::path(Utf8);
}

std::string ReadFile(const fs::path& Path)
{
	if (!fs::is_regular_file(Path))
	{
		throw std::runtime_error("Not a regular file: " + PathToUtf8(Path));
	}
	std::ifstream Stream(Path, std::ios::binary);
	if (!Stream)
	{
		throw std::runtime_error("Cannot open file: " + PathToUtf8(Path));
	}
	std::string Contents((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
	if (Stream.bad())
	{
		throw std::runtime_error("Cannot read file: " + PathToUtf8(Path));
	}
	return Contents;
}

fs::path ResolvePath(const fs::path& Base, std::string_view Text)
{
	if (Text.empty())
	{
		throw std::runtime_error("A required path is empty.");
	}
	return fs::weakly_canonical(fs::absolute(Base / PathFromUtf8(Text)));
}

fs::path ResolveGeneratedPath(const fs::path& Root, std::string_view Text)
{
	const fs::path Relative = PathFromUtf8(Text);
	if (Relative.empty() || Relative.is_absolute() || Relative.has_root_name())
	{
		throw std::runtime_error("Generated paths must be relative: " + std::string(Text));
	}
	for (const fs::path& Part : Relative)
	{
		if (Part == "..")
		{
			throw std::runtime_error("Generated paths cannot contain '..': " + std::string(Text));
		}
	}
	const fs::path Resolved = ResolvePath(Root, Text);
	const fs::path Inside = Resolved.lexically_relative(Root);
	if (Inside.empty() || *Inside.begin() == ".." || Resolved == Root)
	{
		throw std::runtime_error("Generated path escapes its root: " + std::string(Text));
	}
	return Resolved;
}

std::string UniqueSuffix()
{
	static std::atomic<unsigned long long> Counter{0};
	const auto ProcessId =
#ifdef _WIN32
	    GetCurrentProcessId();
#else
	    getpid();
#endif
	return std::to_string(ProcessId) + "-" +
	       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
	       std::to_string(Counter.fetch_add(1));
}

fs::path ExecutablePath()
{
#ifdef _WIN32
	std::wstring Buffer(32768, L'\0');
	const DWORD Size = GetModuleFileNameW(nullptr, Buffer.data(), static_cast<DWORD>(Buffer.size()));
	if (Size == 0 || Size >= Buffer.size())
	{
		throw std::runtime_error("Cannot locate the MBT executable.");
	}
	Buffer.resize(Size);
	return fs::canonical(Buffer);
#elif defined(__APPLE__)
	std::uint32_t Size = 0;
	_NSGetExecutablePath(nullptr, &Size);
	std::string Buffer(Size, '\0');
	if (_NSGetExecutablePath(Buffer.data(), &Size) != 0)
	{
		throw std::runtime_error("Cannot locate the MBT executable.");
	}
	return fs::canonical(Buffer.c_str());
#else
	return fs::canonical("/proc/self/exe");
#endif
}

void ReplaceFileAtomically(const fs::path& Source, const fs::path& Destination)
{
#ifdef _WIN32
	if (!MoveFileExW(Source.c_str(), Destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		throw std::runtime_error("Cannot publish file: " + PathToUtf8(Destination) + " (Windows error " +
		                         std::to_string(GetLastError()) + ").");
	}
#else
	fs::rename(Source, Destination);
#endif
}

void WriteFileAtomically(const fs::path& Path, std::string_view Contents)
{
	fs::create_directories(Path.parent_path());
	fs::path Temporary = Path;
	Temporary += ".tmp-" + UniqueSuffix();
	try
	{
		std::ofstream Stream(Temporary, std::ios::binary | std::ios::trunc);
		Stream.write(Contents.data(), static_cast<std::streamsize>(Contents.size()));
		Stream.close();
		if (!Stream)
		{
			throw std::runtime_error("Cannot write file: " + PathToUtf8(Temporary));
		}
		ReplaceFileAtomically(Temporary, Path);
	}
	catch (...)
	{
		std::error_code Ignored;
		fs::remove(Temporary, Ignored);
		throw;
	}
}

FBuildLock::FBuildLock(const fs::path& Output) : FBuildLock(Output, ".mbt-lock")
{
}

FBuildLock::FBuildLock(const fs::path& Output, std::string_view Suffix)
{
	fs::create_directories(Output.parent_path());
	fs::path LockPath = Output;
	LockPath += Suffix;
#ifdef _WIN32
	const HANDLE File = CreateFileW(LockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
	                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (File == INVALID_HANDLE_VALUE)
	{
		throw std::runtime_error("Cannot lock output (possibly in use): " + PathToUtf8(Output));
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
		throw std::runtime_error("Cannot lock output (possibly in use): " + PathToUtf8(Output));
	}
	Handle = File;
#endif
}

FBuildLock::~FBuildLock()
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
