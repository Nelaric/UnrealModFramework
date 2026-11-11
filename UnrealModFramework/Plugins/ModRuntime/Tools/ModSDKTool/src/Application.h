// Copyright (c) 2026 Nelaric Contributors
/** @file Application.h */
#pragma once

#include <string>
#include <vector>

namespace Mod::SdkTool
{
/// Runs one invocation of MDT.
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

private:
	std::vector<std::string> Arguments;
};
} // namespace Mod::SdkTool
