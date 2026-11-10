// Copyright (c) 2026 Nelaric Contributors
#include "Application.h"

#include "Common/Files/Path.h"
#include "ModBuildTool/src/Cli/CommandLine.h"

#include <exception>
#include <filesystem>
#include <iostream>

namespace Mod::BuildTool
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
		return CommandLine::RunCommandLine(Arguments);
	}
	catch (const std::exception& Error)
	{
		std::cerr << "ModBuildTool: error: " << Error.what() << '\n';
		return 1;
	}
}
} // namespace Mod::BuildTool
