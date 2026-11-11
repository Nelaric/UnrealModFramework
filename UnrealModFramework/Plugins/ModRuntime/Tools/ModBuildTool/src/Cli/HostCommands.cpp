// Copyright (c) 2026 Nelaric Contributors
#include "HostCommands.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/HostBuild.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace Mod::BuildTool
{
using Json = nlohmann::json;

int HostCommands::RunHostCommand(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	const auto& Name = Command.Name;
	const HostBuild Host(Path::PathFromUtf8(Options.at("--context")),
	                     Path::PathFromUtf8(Options.at("--generated-manifest")));
	Json Result;
	if (Name == "build-host")
	{
		Result = Host.Build({});
	}
	else if (Name == "build-planned-host")
	{
		const auto& Fingerprint = Options.at("--input-fingerprint");
		Hash::RequireDigest(Fingerprint, "--input-fingerprint");
		Result = Host.Build(Fingerprint);
	}
	else if (Name == "finalize-host")
	{
		Result = Host.Finalize(Path::PathFromUtf8(Options.at("--link-receipt")));
	}
	else
	{
		Result = Host.Plan();
	}
	std::cout << Result.dump(2) << '\n' << std::flush;
	if (!std::cout)
	{
		throw std::runtime_error("Cannot write the JSON result.");
	}
	return Name == "verify-host" && !Result.at("upToDate").get<bool>() ? 1 : 0;
}
} // namespace Mod::BuildTool
