// Copyright (c) 2026 Nelaric Contributors
#include "HostCommands.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/HostBuild.h"
#include "ModBuildTool/src/Cli/HostCommandResult.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

int HostCommands::RunHostCommand(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	const auto& Name = Command.Name;
	if (!Options.contains("--context") || !Options.contains("--generated-manifest"))
	{
		throw std::runtime_error("Both --context and --generated-manifest are required.");
	}
	fs::path Context = Path::PathFromUtf8(Options.at("--context"));
	fs::path Manifest = Path::PathFromUtf8(Options.at("--generated-manifest"));
	const fs::path Output = Options.contains("--output") ? Path::PathFromUtf8(Options.at("--output")) : fs::path();
	const std::string Expected = Options.contains("--expected-input") ? Options.at("--expected-input") : "";
	if (Name == "finalize-host" && !Options.contains("--link-receipt"))
	{
		throw std::runtime_error("finalize-host requires --link-receipt.");
	}
	const fs::path LinkReceipt =
	    Options.contains("--link-receipt") ? Path::PathFromUtf8(Options.at("--link-receipt")) : fs::path();
	if (!Expected.empty())
	{
		Hash::RequireDigest(Expected, "--expected-input");
	}
	const HostBuild Host(std::move(Context), std::move(Manifest));
	Host.ValidateResultOutput(Output, LinkReceipt);
	const Json Result = HostCommandResult::ExecuteHostAction(Host, Name, Expected, LinkReceipt);
	HostCommandResult::WriteHostResult(Host, Output, LinkReceipt, Result);
	return Name == "verify-host" && !Result.at("upToDate").get<bool>() ? 1 : 0;
}
} // namespace Mod::BuildTool
