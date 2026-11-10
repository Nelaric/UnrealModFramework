// Copyright (c) 2026 Nelaric Contributors
#include "HashCommand.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Build/HostContext.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

int HashCommand::HashInput(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	const auto& Name = Command.Name;
	if (!Options.contains("--file"))
	{
		throw std::runtime_error(Name + " requires --file.");
	}
	const fs::path Input = Path::PathFromUtf8(Options.at("--file"));
	std::cout << (Name == "hash-file" ? Hash::HashFile(Input) : HostContext::HashHostContext(Input)) << '\n';
	return std::cout ? 0 : 1;
}
} // namespace Mod::BuildTool
