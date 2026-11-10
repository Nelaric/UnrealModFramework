// Copyright (c) 2026 Nelaric Contributors
#include "HostCommandResult.h"

#include "Common/Files/File.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

Json HostCommandResult::ExecuteHostAction(const HostBuild& Host, std::string_view Name, std::string_view Expected,
                                          const fs::path& LinkReceipt)
{
	if (Name == "build-host")
	{
		return Host.Build(Expected);
	}
	return Name == "finalize-host" ? Host.Finalize(LinkReceipt) : Host.Plan();
}

void HostCommandResult::WriteHostResult(const HostBuild& Host, const fs::path& Output, const fs::path& LinkReceipt,
                                        const Json& Result)
{
	if (Output.empty())
	{
		std::cout << Result.dump(2) << '\n';
		if (!std::cout)
		{
			throw std::runtime_error("Cannot write the JSON result.");
		}
	}
	else
	{
		Host.ValidateResultOutput(Output, LinkReceipt);
		File::WriteFileAtomically(fs::weakly_canonical(fs::absolute(Output)), Result.dump(2) + "\n");
	}
}
} // namespace Mod::BuildTool
