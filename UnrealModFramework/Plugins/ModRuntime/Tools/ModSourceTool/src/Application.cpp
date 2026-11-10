// Copyright (c) 2026 Nelaric Contributors
#include "Application.h"

#include "ModSourceTool/src/Cli/CommandLine.h"

#include <exception>
#include <filesystem>
#include <iostream>

namespace Mod::SourceTool
{
Application::Application(int Count, char* Argv[]) : Arguments(Argv, Argv + Count)
{
}

#ifdef _WIN32
Application::Application(int Count, wchar_t* Argv[])
{
	for (int Index = 0; Index < Count; ++Index)
	{
		const auto Utf8 = std::filesystem::path(Argv[Index]).u8string();
		Arguments.emplace_back(Utf8.begin(), Utf8.end());
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
		std::cerr << "ModSourceTool: error: " << Error.what() << '\n';
		return 1;
	}
}
} // namespace Mod::SourceTool
