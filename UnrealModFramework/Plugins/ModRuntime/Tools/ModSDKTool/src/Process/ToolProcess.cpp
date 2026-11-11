// Copyright (c) 2026 Nelaric Contributors
#include "ToolProcess.h"

#include "Common/Files/Path.h"
#include "Common/Process/ProcessOutput.h"
#include "ModSDKTool/src/Process/Handle.h"
#include "ModSDKTool/src/Process/ProcessArguments.h"
#include "ModSDKTool/src/Process/ProcessCancellation.h"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

int ToolProcess::RunTool(const fs::path& Executable, const std::vector<std::string>& Arguments,
                         const fs::path& WorkingDirectory, unsigned TimeoutSeconds, const fs::path& StandardOutput)
{
	if (!fs::is_regular_file(Executable) || !fs::is_directory(WorkingDirectory) || TimeoutSeconds == 0 ||
	    TimeoutSeconds > 86400)
	{
		throw std::runtime_error("Invalid executable, working directory, or tool timeout.");
	}
	for (const std::string& Argument : Arguments)
	{
		if (Argument.find_first_of("\r\n") != std::string::npos || Argument.find('\0') != std::string::npos)
		{
			throw std::runtime_error("Tool arguments cannot contain NUL or newline characters.");
		}
	}
	const Mod::BuildTool::ProcessOutput Output(StandardOutput);
#ifdef _WIN32
	std::wstring Command = ProcessArguments::Quote(Mod::BuildTool::Path::PathToUtf8(Executable));
	for (const std::string& Argument : Arguments)
	{
		Command += L" " + ProcessArguments::Quote(Argument);
	}
	if (Command.size() >= 32767)
	{
		throw std::runtime_error("SDK child command exceeds Windows command-line limits.");
	}
	Handle Job{CreateJobObjectW(nullptr, nullptr)};
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};
	Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (Job.Value == nullptr ||
	    !SetInformationJobObject(Job.Value, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
	{
		throw std::runtime_error("Cannot create SDK child process job.");
	}
	SECURITY_ATTRIBUTES Security{sizeof(Security), nullptr, TRUE};
	Handle Input{
	    CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &Security, OPEN_EXISTING, 0, nullptr)};
	Handle Diagnostics;
	if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &Diagnostics.Value,
	                     0, TRUE, DUPLICATE_SAME_ACCESS))
	{
		throw std::runtime_error("Cannot inherit SDK tool diagnostics.");
	}
	STARTUPINFOW Startup{};
	Startup.cb = sizeof(Startup);
	Startup.dwFlags = STARTF_USESTDHANDLES;
	Startup.hStdInput = Input.Value;
	Startup.hStdOutput = Output.GetHandle(Diagnostics.Value);
	Startup.hStdError = Diagnostics.Value;
	PROCESS_INFORMATION Info{};
	if (!CreateProcessW(Executable.c_str(), Command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
	                    nullptr, WorkingDirectory.c_str(), &Startup, &Info))
	{
		throw std::runtime_error("Cannot start SDK tool: " + Mod::BuildTool::Path::PathToUtf8(Executable));
	}
	Handle Process{Info.hProcess};
	Handle Thread{Info.hThread};
	if (!AssignProcessToJobObject(Job.Value, Process.Value))
	{
		TerminateProcess(Process.Value, 1);
		throw std::runtime_error("Cannot attach SDK child to its process job.");
	}
	ProcessCancellation::Start(Job.Value);
	if (ResumeThread(Thread.Value) == static_cast<DWORD>(-1))
	{
		TerminateJobObject(Job.Value, 1);
		ProcessCancellation::Stop();
		throw std::runtime_error("Cannot resume SDK child.");
	}
	const DWORD Wait = WaitForSingleObject(Process.Value, TimeoutSeconds * 1000);
	DWORD Code = 1;
	if (Wait != WAIT_OBJECT_0)
	{
		TerminateJobObject(Job.Value, 124);
		WaitForSingleObject(Process.Value, 5000);
	}
	GetExitCodeProcess(Process.Value, &Code);
	ProcessCancellation::Stop();
	if (Wait == WAIT_TIMEOUT)
	{
		throw std::runtime_error("SDK tool timed out: " + Mod::BuildTool::Path::PathToUtf8(Executable));
	}
	if (Wait != WAIT_OBJECT_0)
	{
		throw std::runtime_error("Cannot wait for SDK child.");
	}
	if (Code == 130)
	{
		throw std::runtime_error("SDK build cancelled.");
	}
	return static_cast<int>(Code);
#else
	std::vector<std::string> Storage{Mod::BuildTool::Path::PathToUtf8(Executable)};
	Storage.insert(Storage.end(), Arguments.begin(), Arguments.end());
	std::vector<char*> Argv;
	for (std::string& Value : Storage)
	{
		Argv.push_back(Value.data());
	}
	Argv.push_back(nullptr);
	struct sigaction PreviousInt
	{
	};
	struct sigaction PreviousTerm
	{
	};
	ProcessCancellation::Start(PreviousInt, PreviousTerm);
	const pid_t Child = fork();
	if (Child == 0)
	{
		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		setpgid(0, 0);
		const int Input = open("/dev/null", O_RDONLY);
		if (Input < 0 || dup2(Input, STDIN_FILENO) < 0 || chdir(WorkingDirectory.c_str()) != 0 ||
		    !Output.RedirectInChild())
		{
			_exit(126);
		}
		if (Input != STDIN_FILENO)
		{
			close(Input);
		}
		execv(Executable.c_str(), Argv.data());
		_exit(127);
	}
	int Status = 0;
	bool bTimedOut = false;
	if (Child > 0)
	{
		setpgid(Child, Child);
		ProcessCancellation::TrackChild(Child);
		const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(TimeoutSeconds);
		for (;;)
		{
			const pid_t Result = waitpid(Child, &Status, WNOHANG);
			if (Result == Child)
			{
				break;
			}
			if (Result < 0 && errno != EINTR)
			{
				Status = -1;
				break;
			}
			if (ProcessCancellation::WasCancelled() || std::chrono::steady_clock::now() >= Deadline)
			{
				bTimedOut = !ProcessCancellation::WasCancelled();
				kill(-Child, SIGKILL);
				while (waitpid(Child, &Status, 0) < 0 && errno == EINTR)
				{
				}
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}
	ProcessCancellation::Stop(PreviousInt, PreviousTerm);
	if (Child < 0 || Status == -1 || bTimedOut)
	{
		throw std::runtime_error("SDK tool failed to start, wait, or meet its deadline: " +
		                         Mod::BuildTool::Path::PathToUtf8(Executable));
	}
	const int ExitCode = WIFEXITED(Status) ? WEXITSTATUS(Status) : 128 + WTERMSIG(Status);
	if (ProcessCancellation::WasCancelled() || ExitCode == 130)
	{
		throw std::runtime_error("SDK build cancelled.");
	}
	return ExitCode;
#endif
}
} // namespace Mod::SdkTool
