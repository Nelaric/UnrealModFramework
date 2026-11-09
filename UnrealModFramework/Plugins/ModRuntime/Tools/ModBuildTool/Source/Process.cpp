// Copyright (c) 2026 Nelaric Contributors
#include "Process.h"

#include <cerrno>
#include <atomic>
#include <cwctype>
#include <stdexcept>

#ifdef _WIN32
#include <Windows.h>
#else
#include <csignal>
#include <cstdlib>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace Mod::BuildTool
{
namespace
{
std::string QuoteMsvc(std::string_view Value)
{
	std::string Result = "\"";
	std::size_t Slashes = 0;
	for (char Character : Value)
	{
		if (Character == '\\')
		{
			++Slashes;
			continue;
		}
		Result.append(Slashes * (Character == '"' ? 2 : 1), '\\');
		Slashes = 0;
		if (Character == '"')
		{
			Result.push_back('\\');
		}
		Result.push_back(Character);
	}
	Result.append(Slashes * 2, '\\');
	Result.push_back('"');
	return Result;
}

void RequireArgument(std::string_view Value)
{
	if (Value.find_first_of("\r\n") != std::string_view::npos || Value.find('\0') != std::string_view::npos)
	{
		throw std::runtime_error("Tool arguments cannot contain NUL or newline characters.");
	}
}

#ifdef _WIN32
std::atomic<HANDLE> ActiveJob{nullptr};

BOOL WINAPI CancelProcess(DWORD Event)
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

struct FEnvironmentKeyLess
{
	bool operator()(const std::wstring& Left, const std::wstring& Right) const
	{
		return _wcsicmp(Left.c_str(), Right.c_str()) < 0;
	}
};

std::vector<wchar_t> MakeEnvironment(const std::map<std::string, std::string>& Overrides)
{
	std::map<std::wstring, std::wstring, FEnvironmentKeyLess> Values;
	for (const auto& [Name, Value] : Overrides)
	{
		Values[PathFromUtf8(Name).wstring()] = PathFromUtf8(Value).wstring();
	}
	std::vector<wchar_t> Result;
	if (Values.empty())
	{
		Result.push_back(L'\0');
	}
	for (const auto& [Name, Value] : Values)
	{
		const std::wstring Item = Name + L"=" + Value;
		Result.insert(Result.end(), Item.begin(), Item.end());
		Result.push_back(L'\0');
	}
	Result.push_back(L'\0');
	return Result;
}
#else
volatile std::sig_atomic_t ActiveChild = 0;
volatile std::sig_atomic_t CancelSignal = 0;

void CancelProcess(int Signal)
{
	CancelSignal = Signal;
	if (ActiveChild > 0)
	{
		kill(-static_cast<pid_t>(ActiveChild), Signal);
	}
}
#endif
} // namespace

std::map<std::string, std::string> CaptureEnvironment()
{
	std::map<std::string, std::string> Result;
#ifdef _WIN32
	LPWCH Block = GetEnvironmentStringsW();
	if (Block == nullptr)
	{
		throw std::runtime_error("Cannot read the process environment.");
	}
	for (const wchar_t* Entry = Block; *Entry != L'\0';)
	{
		const std::wstring Item(Entry);
		const std::size_t Equals = Item.find(L'=', 1);
		if (Equals != std::wstring::npos)
		{
			std::wstring Name = Item.substr(0, Equals);
			for (wchar_t& Character : Name)
			{
				Character = static_cast<wchar_t>(std::towupper(Character));
			}
			const std::u8string Utf8Name = fs::path(Name).u8string();
			const std::u8string Utf8Value = fs::path(Item.substr(Equals + 1)).u8string();
			Result[std::string(Utf8Name.begin(), Utf8Name.end())] = std::string(Utf8Value.begin(), Utf8Value.end());
		}
		Entry += Item.size() + 1;
	}
	FreeEnvironmentStringsW(Block);
#else
	for (char** Entry = environ; Entry != nullptr && *Entry != nullptr; ++Entry)
	{
		const std::string Item(*Entry);
		const std::size_t Equals = Item.find('=');
		if (Equals != std::string::npos)
		{
			Result[Item.substr(0, Equals)] = Item.substr(Equals + 1);
		}
	}
#endif
	return Result;
}

void WriteResponseFile(const fs::path& Path, const std::vector<std::string>& Arguments, std::string_view Format)
{
	std::string Text;
	for (const std::string& Argument : Arguments)
	{
		RequireArgument(Argument);
		if (Format == "msvc")
		{
			// MSVC requires an option and its separate value on the same line.
			Text += QuoteMsvc(Argument) + " ";
		}
		else if (Format == "gnu")
		{
			Text.push_back('"');
			for (char Character : Argument)
			{
				if (Character == '\\' || Character == '"')
				{
					Text.push_back('\\');
				}
				Text.push_back(Character);
			}
			Text += "\"\n";
		}
		else
		{
			throw std::runtime_error("Unknown response format: " + std::string(Format));
		}
	}
	if (Format == "msvc")
	{
		Text += "\r\n";
#ifdef _WIN32
		const std::wstring Wide = PathFromUtf8(Text).wstring();
		std::string Encoded = "\xff\xfe";
		for (wchar_t Character : Wide)
		{
			Encoded.push_back(static_cast<char>(Character & 255));
			Encoded.push_back(static_cast<char>((Character >> 8) & 255));
		}
		WriteFileAtomically(Path, Encoded);
#else
		throw std::runtime_error("MSVC response files require a Windows host.");
#endif
	}
	else
	{
		WriteFileAtomically(Path, Text);
	}
}

int RunProcess(const fs::path& Executable, const std::vector<std::string>& Arguments, const fs::path& WorkingDirectory,
               const std::map<std::string, std::string>& Environment)
{
	for (const std::string& Argument : Arguments)
	{
		RequireArgument(Argument);
	}
#ifdef _WIN32
	std::wstring Command = PathFromUtf8(QuoteMsvc(PathToUtf8(Executable))).wstring();
	for (const std::string& Argument : Arguments)
	{
		Command += L" " + PathFromUtf8(QuoteMsvc(Argument)).wstring();
	}
	if (Command.size() >= 32767)
	{
		throw std::runtime_error("Process command line exceeds Windows limits; use a response file.");
	}
	FHandle Job{CreateJobObjectW(nullptr, nullptr)};
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits{};
	Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (Job.Value == nullptr ||
	    !SetInformationJobObject(Job.Value, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)))
	{
		throw std::runtime_error("Cannot create a tool process job.");
	}
	SECURITY_ATTRIBUTES Security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
	FHandle Input{
	    CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &Security, OPEN_EXISTING, 0, nullptr)};
	FHandle Error;
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
	std::vector<wchar_t> EnvironmentBlock = MakeEnvironment(Environment);
	if (!CreateProcessW(Executable.c_str(), Command.data(), nullptr, nullptr, TRUE,
	                    CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, EnvironmentBlock.data(),
	                    WorkingDirectory.c_str(), &Startup, &Info))
	{
		throw std::runtime_error("Cannot start tool: " + PathToUtf8(Executable) + " (Windows error " +
		                         std::to_string(GetLastError()) + ").");
	}
	FHandle Process{Info.hProcess};
	FHandle Thread{Info.hThread};
	if (!AssignProcessToJobObject(Job.Value, Process.Value))
	{
		TerminateProcess(Process.Value, 1);
		throw std::runtime_error("Cannot attach tool process to its cancellation job.");
	}
	ActiveJob = Job.Value;
	SetConsoleCtrlHandler(CancelProcess, TRUE);
	if (ResumeThread(Thread.Value) == static_cast<DWORD>(-1))
	{
		ActiveJob = nullptr;
		SetConsoleCtrlHandler(CancelProcess, FALSE);
		throw std::runtime_error("Cannot resume tool process.");
	}
	const DWORD WaitResult = WaitForSingleObject(Process.Value, INFINITE);
	DWORD ExitCode = 1;
	const BOOL HasExitCode = GetExitCodeProcess(Process.Value, &ExitCode);
	ActiveJob = nullptr;
	SetConsoleCtrlHandler(CancelProcess, FALSE);
	if (WaitResult != WAIT_OBJECT_0 || !HasExitCode)
	{
		throw std::runtime_error("Cannot wait for tool completion.");
	}
	return static_cast<int>(ExitCode);
#else
	std::vector<std::string> Storage{PathToUtf8(Executable)};
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
	struct sigaction Handler
	{
	};
	struct sigaction PreviousInt
	{
	};
	struct sigaction PreviousTerm
	{
	};
	Handler.sa_handler = CancelProcess;
	sigemptyset(&Handler.sa_mask);
	sigaction(SIGINT, &Handler, &PreviousInt);
	sigaction(SIGTERM, &Handler, &PreviousTerm);
	CancelSignal = 0;
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
		ActiveChild = static_cast<std::sig_atomic_t>(Child);
		if (CancelSignal != 0)
		{
			kill(-Child, CancelSignal);
		}
		while (waitpid(Child, &Status, 0) < 0)
		{
			if (errno != EINTR)
			{
				Status = -1;
				break;
			}
		}
	}
	ActiveChild = 0;
	sigaction(SIGINT, &PreviousInt, nullptr);
	sigaction(SIGTERM, &PreviousTerm, nullptr);
	if (Child < 0 || Status == -1)
	{
		throw std::runtime_error("Cannot execute or wait for tool: " + PathToUtf8(Executable));
	}
	return WIFEXITED(Status) ? WEXITSTATUS(Status) : 128 + WTERMSIG(Status);
#endif
}
} // namespace Mod::BuildTool
