// Copyright (c) 2026 Nelaric Contributors
#include "Executable.h"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

fs::path Executable::ExecutablePath()
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
} // namespace Mod::BuildTool
