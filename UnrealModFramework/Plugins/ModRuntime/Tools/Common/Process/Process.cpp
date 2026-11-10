// Copyright (c) 2026 Nelaric Contributors
#include "Process.h"

#include "Common/Files/Path.h"
#include "Common/Process/EnvironmentBlock.h"
#include "Common/Process/Handle.h"
#include "Common/Process/ProcessArguments.h"
#include "Common/Process/ProcessCancellation.h"

#include <cerrno>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

int Process::RunProcess(const fs::path& Executable, const std::vector<std::string>& Arguments,
                        const fs::path& WorkingDirectory, const std::map<std::string, std::string>& Environment)
{
	for (const std::string& Argument : Arguments)
	{
		ProcessArguments::RequireArgument(Argument);
	}
#ifdef _WIN32
	std::wstring Command = Path::PathFromUtf8(ProcessArguments::QuoteMsvc(Path::PathToUtf8(Executable))).wstring();
	for (const std::string& Argument : Arguments)
	{
		Command += L" " + Path::PathFromUtf8(ProcessArguments::QuoteMsvc(Argument)).wstring();
	}
	if (Command.size() >= 32767)
	{
		throw std::runtime_error("Process command line exceeds Windows limits; use a response file.");
	}
	Handle Job{CreateJobObjectW(nullptr, nullptr)};
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};
	Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (Job.Value == nullptr ||
	    !SetInformationJobObject(Job.Value, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
	{
		throw std::runtime_error("Cannot create a tool process job.");
	}
	SECURITY_ATTRIBUTES Security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	Handle Input{
	    CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &Security, OPEN_EXISTING, 0, nullptr)};
	Handle Error;
	if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_ERROR_HANDLE), GetCurrentProcess(), &Error.Value, 0,
	                     TRUE, DUPLICATE_SAME_ACCESS))
	{
		throw std::runtime_error("Cannot inherit tool diagnostics.");
	}
	STARTUPINFOW Startup{};
	Startup.cb = sizeof(Startup);
	Startup.dwFlags = STARTF_USESTDHANDLES;
	Startup.hStdInput = Input.Value;
	Startup.hStdOutput = Error.Value;
	Startup.hStdError = Error.Value;
	PROCESS_INFORMATION Info{};
	std::vector<wchar_t> EnvironmentBlock = EnvironmentBlock::MakeEnvironment(Environment);
	if (!CreateProcessW(Executable.c_str(), Command.data(), nullptr, nullptr, TRUE,
	                    CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, EnvironmentBlock.data(),
	                    WorkingDirectory.c_str(), &Startup, &Info))
	{
		throw std::runtime_error("Cannot start tool: " + Path::PathToUtf8(Executable) + " (Windows error " +
		                         std::to_string(GetLastError()) + ").");
	}
	Handle Process{Info.hProcess};
	Handle Thread{Info.hThread};
	if (!AssignProcessToJobObject(Job.Value, Process.Value))
	{
		TerminateProcess(Process.Value, 1);
		throw std::runtime_error("Cannot attach tool process to its cancellation job.");
	}
	ProcessCancellation::Start(Job.Value);
	if (ResumeThread(Thread.Value) == static_cast<DWORD>(-1))
	{
		ProcessCancellation::Stop();
		throw std::runtime_error("Cannot resume tool process.");
	}
	const DWORD WaitResult = WaitForSingleObject(Process.Value, INFINITE);
	DWORD ExitCode = 1;
	const BOOL HasExitCode = GetExitCodeProcess(Process.Value, &ExitCode);
	ProcessCancellation::Stop();
	if (WaitResult != WAIT_OBJECT_0 || !HasExitCode)
	{
		throw std::runtime_error("Cannot wait for tool completion.");
	}
	return static_cast<int>(ExitCode);
#else
	std::vector<std::string> Storage{Path::PathToUtf8(Executable)};
	Storage.insert(Storage.end(), Arguments.begin(), Arguments.end());
	std::vector<char*> Argv;
	for (std::string& Argument : Storage)
	{
		Argv.push_back(Argument.data());
	}
	Argv.push_back(nullptr);
	std::vector<std::string> EnvironmentStorage;
	for (const auto& [Name, Value] : Environment)
	{
		EnvironmentStorage.push_back(Name + "=" + Value);
	}
	std::vector<char*> Envp;
	for (std::string& Item : EnvironmentStorage)
	{
		Envp.push_back(Item.data());
	}
	Envp.push_back(nullptr);
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
		if (chdir(WorkingDirectory.c_str()) != 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
		{
			_exit(126);
		}
		execve(Executable.c_str(), Argv.data(), Envp.data());
		_exit(127);
	}
	int Status = 0;
	if (Child > 0)
	{
		setpgid(Child, Child);
		ProcessCancellation::TrackChild(Child);
		while (waitpid(Child, &Status, 0) < 0)
		{
			if (errno != EINTR)
			{
				Status = -1;
				break;
			}
		}
	}
	ProcessCancellation::Stop(PreviousInt, PreviousTerm);
	if (Child < 0 || Status == -1)
	{
		throw std::runtime_error("Cannot execute or wait for tool: " + Path::PathToUtf8(Executable));
	}
	return WIFEXITED(Status) ? WEXITSTATUS(Status) : 128 + WTERMSIG(Status);
#endif
}
} // namespace Mod::BuildTool
