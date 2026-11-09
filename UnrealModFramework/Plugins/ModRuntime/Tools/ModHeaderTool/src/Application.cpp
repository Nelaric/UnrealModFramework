// Copyright (c) 2026 Nelaric Contributors
#include "ModHeaderTool/Application.h"

#include "ModHeaderTool/CommandLine.h"
#include "ModHeaderTool/FileSystem.h"
#include "ModHeaderTool/Generation.h"
#include "ModHeaderTool/ScanReport.h"
#include "ModHeaderTool/Scanner.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace
{
int Run(const std::vector<std::string>& Arguments)
{
	try
	{
		const FCommandLineOptions Options = ParseArguments(Arguments);
		if (Options.bHelp)
		{
			PrintHelp(std::cout);
			return 0;
		}
		if (Options.bGenerate)
		{
			Generate(Options.BuildInput, Options.OutputDirectory, Options.Options, Options.ClangLibrary,
			         Options.HostContexts, Options.LifecycleSource);
			return 0;
		}
		const FScanResult Result = ScanProject(Options.Project, Options.Options);
		if (!Result.Diagnostics.empty())
		{
			for (const std::string& Diagnostic : Result.Diagnostics)
			{
				std::cerr << Diagnostic << '\n';
			}
			return 1;
		}
		WriteScanReport(Result, Options.Output);
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << "ModHeaderTool: error: " << Error.what() << '\n';
		return 1;
	}
}
} // namespace

int RunApplication(int Count, char* Arguments[])
{
	return Run(std::vector<std::string>(Arguments, Arguments + Count));
}

#ifdef _WIN32
int RunApplication(int Count, wchar_t* Arguments[])
{
	std::vector<std::string> Utf8Arguments;
	for (int Index = 0; Index < Count; ++Index)
	{
		Utf8Arguments.push_back(PathToUtf8(fs::path(Arguments[Index])));
	}
	return Run(Utf8Arguments);
}
#endif
} // namespace Mod::HeaderTool
