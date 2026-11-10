// Copyright (c) 2026 Nelaric Contributors
#include "ProcessCancellation.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <csignal>
#endif

namespace Mod::SdkTool
{
#ifdef _WIN32
void ProcessCancellation::Start(HANDLE Job)
{
	ActiveJob = Job;
	SetConsoleCtrlHandler(CancelChild, TRUE);
}

void ProcessCancellation::Stop()
{
	ActiveJob = nullptr;
	SetConsoleCtrlHandler(CancelChild, FALSE);
}

BOOL WINAPI ProcessCancellation::CancelChild(DWORD Event)
{
	const HANDLE Job = ActiveJob.load();
	if ((Event == CTRL_C_EVENT || Event == CTRL_BREAK_EVENT) && Job != nullptr)
	{
		TerminateJobObject(Job, 130);
		return TRUE;
	}
	return FALSE;
}
#endif

#ifndef _WIN32
void ProcessCancellation::Start(struct sigaction& PreviousInt, struct sigaction& PreviousTerm)
{
	struct sigaction Handler
	{
	};
	Handler.sa_handler = CancelChild;
	sigemptyset(&Handler.sa_mask);
	sigaction(SIGINT, &Handler, &PreviousInt);
	sigaction(SIGTERM, &Handler, &PreviousTerm);
	Cancelled = 0;
}

void ProcessCancellation::TrackChild(pid_t Child)
{
	ChildGroup = static_cast<std::sig_atomic_t>(Child);
}

void ProcessCancellation::Stop(const struct sigaction& PreviousInt, const struct sigaction& PreviousTerm)
{
	ChildGroup = 0;
	sigaction(SIGINT, &PreviousInt, nullptr);
	sigaction(SIGTERM, &PreviousTerm, nullptr);
}

bool ProcessCancellation::WasCancelled()
{
	return Cancelled != 0;
}

void ProcessCancellation::CancelChild(int Signal)
{
	Cancelled = Signal;
	if (ChildGroup > 0)
	{
		kill(-static_cast<pid_t>(ChildGroup), Signal);
	}
}
#endif
} // namespace Mod::SdkTool
