// Copyright (c) 2026 Nelaric Contributors
#include "TemporaryName.h"

#include <atomic>
#include <chrono>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif

namespace Mod::BuildTool
{
std::string TemporaryName::UniqueSuffix()
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
} // namespace Mod::BuildTool
