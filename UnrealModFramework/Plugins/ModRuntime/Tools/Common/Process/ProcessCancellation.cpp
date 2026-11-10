// Copyright (c) 2026 Nelaric Contributors
#include "ProcessCancellation.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <csignal>
#endif

namespace Mod::BuildTool
{
#ifdef _WIN32
void ProcessCancellation::Start(HANDLE Job)
{
	ActiveJob = Job;
	SetConsoleCtrlHandler(CancelProcess, TRUE);
}

void ProcessCancellation::Stop()
{
	ActiveJob = nullptr;
	SetConsoleCtrlHandler(CancelProcess, FALSE);
}

BOOL WINAPI ProcessCancellation::CancelProcess(DWORD Event)
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
	Handler.sa_handler = CancelProcess;
	sigemptyset(&Handler.sa_mask);
	sigaction(SIGINT, &Handler, &PreviousInt);
	sigaction(SIGTERM, &Handler, &PreviousTerm);
	CancelSignal = 0;
}

void ProcessCancellation::TrackChild(pid_t Child)
{
	ActiveChild = static_cast<std::sig_atomic_t>(Child);
	if (CancelSignal != 0)
	{
		kill(-Child, CancelSignal);
	}
}

void ProcessCancellation::Stop(const struct sigaction& PreviousInt, const struct sigaction& PreviousTerm)
{
	ActiveChild = 0;
	sigaction(SIGINT, &PreviousInt, nullptr);
	sigaction(SIGTERM, &PreviousTerm, nullptr);
}

void ProcessCancellation::CancelProcess(int Signal)
{
	CancelSignal = Signal;
	if (ActiveChild > 0)
	{
		kill(-static_cast<pid_t>(ActiveChild), Signal);
	}
}
#endif
} // namespace Mod::BuildTool
