// Copyright (c) 2026 Nelaric Contributors
/** @file ResultOutput.h */
#pragma once

#include "Common/Files/File.h"

#include <exception>
#include <filesystem>
#include <iostream>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class ResultOutput
{
public:
	static int ReportFailure(const std::exception& Error, const fs::path& Output);
	template <typename ActionType> static int ExecuteWithResult(const fs::path& Output, ActionType Action);
};

template <typename ActionType> int ResultOutput::ExecuteWithResult(const fs::path& Output, ActionType Action)
{
	try
	{
		const auto Result = Action();
		if (Output.empty())
		{
			std::cout << Result.dump(2) << '\n';
			return std::cout ? 0 : 1;
		}
		Mod::BuildTool::File::WriteFileAtomically(Output, Result.dump(2) + "\n");
		return 0;
	}
	catch (const std::exception& Error)
	{
		return ReportFailure(Error, Output);
	}
}
} // namespace Mod::SdkTool
