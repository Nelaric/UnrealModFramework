// Copyright (c) 2026 Nelaric Contributors
#include "Sdk.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Mod::SdkTool
{
namespace
{
#ifdef _WIN32
std::atomic<HANDLE> ActiveJob{nullptr};

BOOL WINAPI CancelChild(DWORD Event)
{
	const HANDLE Job = ActiveJob.load();
	if ((Event == CTRL_C_EVENT || Event == CTRL_BREAK_EVENT) && Job != nullptr)
	{
		TerminateJobObject(Job, 130);
		return TRUE;
	}
	return FALSE;
}

struct FHandle
{
	HANDLE Value = nullptr;
	~FHandle()
	{
		if (Value != nullptr && Value != INVALID_HANDLE_VALUE)
		{
			CloseHandle(Value);
		}
	}
};

std::wstring Quote(std::string_view Value)
{
	const std::wstring Wide = Mod::BuildTool::PathFromUtf8(Value).wstring();
	std::wstring Result = L"\"";
	std::size_t Slashes = 0;
	for (wchar_t Character : Wide)
	{
		if (Character == L'\\')
		{
			++Slashes;
			continue;
		}
		Result.append(Slashes * (Character == L'"' ? 2 : 1), L'\\');
		Slashes = 0;
		if (Character == L'"')
		{
			Result.push_back(L'\\');
		}
		Result.push_back(Character);
	}
	Result.append(Slashes * 2, L'\\');
	return Result + L'"';
}
#else
volatile std::sig_atomic_t ChildGroup = 0;
volatile std::sig_atomic_t Cancelled = 0;

void CancelChild(int Signal)
{
	Cancelled = Signal;
	if (ChildGroup > 0)
	{
		kill(-static_cast<pid_t>(ChildGroup), Signal);
	}
}
#endif
} // namespace

int RunTool(const fs::path& Executable, const std::vector<std::string>& Arguments, const fs::path& WorkingDirectory,
            unsigned TimeoutSeconds)
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
#ifdef _WIN32
	std::wstring Command = Quote(Mod::BuildTool::PathToUtf8(Executable));
	for (const std::string& Argument : Arguments)
	{
		Command += L" " + Quote(Argument);
	}
	if (Command.size() >= 32767)
	{
		throw std::runtime_error("SDK child command exceeds Windows command-line limits.");
	}
	FHandle Job{CreateJobObjectW(nullptr, nullptr)};
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};
	Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (Job.Value == nullptr ||
	    !SetInformationJobObject(Job.Value, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
	{
		throw std::runtime_error("Cannot create SDK child process job.");
	}
	SECURITY_ATTRIBUTES Security{sizeof(Security), nullptr, TRUE};
	FHandle Input{
	    CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &Security, OPEN_EXISTING, 0, nullptr)};
	FHandle Diagnostics;
	if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &Diagnostics.Value,
	                     0, TRUE, DUPLICATE_SAME_ACCESS))
	{
		throw std::runtime_error("Cannot inherit SDK tool diagnostics.");
	}
	STARTUPINFOW Startup{};
	Startup.cb = sizeof(Startup);
	Startup.dwFlags = STARTF_USESTDHANDLES;
	Startup.hStdInput = Input.Value;
	Startup.hStdOutput = Diagnostics.Value;
	Startup.hStdError = Diagnostics.Value;
	PROCESS_INFORMATION Info{};
	if (!CreateProcessW(Executable.c_str(), Command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
	                    nullptr, WorkingDirectory.c_str(), &Startup, &Info))
	{
		throw std::runtime_error("Cannot start SDK tool: " + Mod::BuildTool::PathToUtf8(Executable));
	}
	FHandle Process{Info.hProcess};
	FHandle Thread{Info.hThread};
	if (!AssignProcessToJobObject(Job.Value, Process.Value))
	{
		TerminateProcess(Process.Value, 1);
		throw std::runtime_error("Cannot attach SDK child to its process job.");
	}
	ActiveJob = Job.Value;
	SetConsoleCtrlHandler(CancelChild, TRUE);
	if (ResumeThread(Thread.Value) == static_cast<DWORD>(-1))
	{
		TerminateJobObject(Job.Value, 1);
		ActiveJob = nullptr;
		SetConsoleCtrlHandler(CancelChild, FALSE);
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
	ActiveJob = nullptr;
	SetConsoleCtrlHandler(CancelChild, FALSE);
	if (Wait == WAIT_TIMEOUT)
	{
		throw std::runtime_error("SDK tool timed out: " + Mod::BuildTool::PathToUtf8(Executable));
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
	std::vector<std::string> Storage{Mod::BuildTool::PathToUtf8(Executable)};
	Storage.insert(Storage.end(), Arguments.begin(), Arguments.end());
	std::vector<char*> Argv;
	for (std::string& Value : Storage)
	{
		Argv.push_back(Value.data());
	}
	Argv.push_back(nullptr);
	struct sigaction Handler
	{
	};
	struct sigaction PreviousInt
	{
	};
	struct sigaction PreviousTerm
	{
	};
	Handler.sa_handler = CancelChild;
	sigemptyset(&Handler.sa_mask);
	sigaction(SIGINT, &Handler, &PreviousInt);
	sigaction(SIGTERM, &Handler, &PreviousTerm);
	Cancelled = 0;
	const pid_t Child = fork();
	if (Child == 0)
	{
		signal(SIGINT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		setpgid(0, 0);
		const int Input = open("/dev/null", O_RDONLY);
		if (Input < 0 || dup2(Input, STDIN_FILENO) < 0 || chdir(WorkingDirectory.c_str()) != 0 ||
		    dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
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
		ChildGroup = static_cast<std::sig_atomic_t>(Child);
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
			if (Cancelled != 0 || std::chrono::steady_clock::now() >= Deadline)
			{
				bTimedOut = Cancelled == 0;
				kill(-Child, SIGKILL);
				while (waitpid(Child, &Status, 0) < 0 && errno == EINTR)
				{
				}
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}
	ChildGroup = 0;
	sigaction(SIGINT, &PreviousInt, nullptr);
	sigaction(SIGTERM, &PreviousTerm, nullptr);
	if (Child < 0 || Status == -1 || bTimedOut)
	{
		throw std::runtime_error("SDK tool failed to start, wait, or meet its deadline: " +
		                         Mod::BuildTool::PathToUtf8(Executable));
	}
	const int ExitCode = WIFEXITED(Status) ? WEXITSTATUS(Status) : 128 + WTERMSIG(Status);
	if (Cancelled != 0 || ExitCode == 130)
	{
		throw std::runtime_error("SDK build cancelled.");
	}
	return ExitCode;
#endif
}
} // namespace Mod::SdkTool
