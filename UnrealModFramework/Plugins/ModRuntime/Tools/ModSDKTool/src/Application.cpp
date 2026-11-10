// Copyright (c) 2026 Nelaric Contributors
#include "Application.h"

#include "Common/Files/Path.h"
#include "ModSDKTool/src/Cli/CommandDiagnostics.h"
#include "ModSDKTool/src/Cli/CommandLine.h"

#include <exception>
#include <filesystem>

namespace Mod::SdkTool
{
Application::Application(int Count, char* Argv[]) : Arguments(Argv, Argv + Count)
{
}

#ifdef _WIN32
Application::Application(int Count, wchar_t* Argv[])
{
	for (int Index = 0; Index < Count; ++Index)
	{
		Arguments.push_back(Mod::BuildTool::Path::PathToUtf8(std::filesystem::path(Argv[Index])));
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
		return CommandDiagnostics::ReportCommandError(Error);
	}
}
} // namespace Mod::SdkTool
