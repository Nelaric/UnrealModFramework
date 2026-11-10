// Copyright (c) 2026 Nelaric Contributors
/** @file Application.h */
#pragma once

#include <string>
#include <vector>

namespace Mod::HeaderTool
{
/// Runs one invocation of ModHeaderTool.
class Application
{
public:
	/// Copies the native narrow command-line arguments.
	Application(int Count, char* Argv[]);

#ifdef _WIN32
	/// Converts the native Windows command line to UTF-8.
	Application(int Count, wchar_t* Argv[]);
#endif

	/// Executes the requested command and returns its exit code.
	int Run() const;

	/// Runs the command-line application with native narrow arguments.
	static int RunApplication(int Count, char* Arguments[]);

#ifdef _WIN32
	/// Runs the Windows application with native UTF-16 arguments.
	static int RunApplication(int Count, wchar_t* Arguments[]);
#endif

private:
	std::vector<std::string> Arguments;
};
} // namespace Mod::HeaderTool
