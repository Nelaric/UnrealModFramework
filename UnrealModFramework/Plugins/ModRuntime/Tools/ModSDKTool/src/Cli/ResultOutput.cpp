// Copyright (c) 2026 Nelaric Contributors
#include "ResultOutput.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

int ResultOutput::ReportFailure(const std::exception& Error, const fs::path& Output)
{
	const bool bCancelled = std::string_view(Error.what()) == "SDK build cancelled.";
	const Json Failure = {{"format", "UMF.SdkResult"},
	                      {"schemaVersion", 1},
	                      {"status", bCancelled ? "Cancelled" : "Failed"},
	                      {"message", Error.what()}};
	std::cerr << "ModSDKTool: error: " << Error.what() << '\n';
	if (!Output.empty())
	{
		try
		{
			Mod::BuildTool::File::WriteFileAtomically(
			    Output, Failure.dump(2, ' ', false, Json::error_handler_t::replace) + "\n");
		}
		catch (const std::exception& WriteError)
		{
			std::cerr << "ModSDKTool: result: " << WriteError.what() << '\n';
		}
	}
	return bCancelled ? 130 : 1;
}
} // namespace Mod::SdkTool
