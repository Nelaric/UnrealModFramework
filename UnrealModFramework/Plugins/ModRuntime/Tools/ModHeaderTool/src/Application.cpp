// Copyright (c) 2026 Nelaric Contributors
#include "Application.h"

#include "ModHeaderTool/src/Cli/CommandExecution.h"
#include "ModHeaderTool/src/Cli/CommandLine.h"
#include "ModHeaderTool/src/Files/Path.h"

#include <exception>
#include <filesystem>
#include <iostream>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

Application::Application(int Count, char* Argv[]) : Arguments(Argv, Argv + Count)
{
}

#ifdef _WIN32
Application::Application(int Count, wchar_t* Argv[])
{
	for (int Index = 0; Index < Count; ++Index)
	{
		Arguments.push_back(Path::PathToUtf8(fs::path(Argv[Index])));
	}
}
#endif

int Application::Run() const
{
	try
	{
		return CommandExecution::ExecuteCommand(CommandLine::ParseArguments(Arguments));
	}
	catch (const std::exception& Error)
	{
		std::cerr << "ModHeaderTool: error: " << Error.what() << '\n';
		return 1;
	}
}

int Application::RunApplication(int Count, char* Arguments[])
{
	return Application(Count, Arguments).Run();
}

#ifdef _WIN32
int Application::RunApplication(int Count, wchar_t* Arguments[])
{
	return Application(Count, Arguments).Run();
}
#endif
} // namespace Mod::HeaderTool
