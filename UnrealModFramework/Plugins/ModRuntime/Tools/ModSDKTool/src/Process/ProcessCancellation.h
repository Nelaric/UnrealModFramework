// Copyright (c) 2026 Nelaric Contributors
/** @file ProcessCancellation.h */
#pragma once

#include <atomic>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <csignal>
#endif

namespace Mod::SdkTool
{
#ifdef _WIN32
class ProcessCancellation
{
public:
	static void Start(HANDLE Job);

	static void Stop();

private:
	inline static std::atomic<HANDLE> ActiveJob{nullptr};

	static BOOL WINAPI CancelChild(DWORD Event);
};

#else
class ProcessCancellation
{
public:
	static void Start(struct sigaction& PreviousInt, struct sigaction& PreviousTerm);

	static void TrackChild(pid_t Child);

	static void Stop(const struct sigaction& PreviousInt, const struct sigaction& PreviousTerm);

	static bool WasCancelled();

private:
	inline static volatile std::sig_atomic_t ChildGroup = 0;
	inline static volatile std::sig_atomic_t Cancelled = 0;

	static void CancelChild(int Signal);
};
#endif
} // namespace Mod::SdkTool
