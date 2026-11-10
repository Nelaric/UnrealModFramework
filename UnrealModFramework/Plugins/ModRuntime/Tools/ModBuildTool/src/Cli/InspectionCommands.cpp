// Copyright (c) 2026 Nelaric Contributors
#include "InspectionCommands.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Archive/ArchiveInspector.h"
#include "ModBuildTool/src/Binary/BinaryInspector.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

int InspectionCommands::InspectArchive(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	for (const char* Required : {"--file", "--platform", "--architecture", "--anchor"})
	{
		if (!Options.contains(Required))
		{
			throw std::runtime_error(std::string("inspect-archive requires ") + Required);
		}
	}
	const fs::path Archive = Path::PathFromUtf8(Options.at("--file"));
	auto ExtraSymbols = Command.Symbols;
	ExtraSymbols.insert(ExtraSymbols.begin(), Options.at("--anchor"));
	ArchiveInspector::ValidateHostArchive(Archive, Options.at("--platform"), Options.at("--architecture"),
	                                      ExtraSymbols);
	std::cout << Json({{"format", "UMF.ArchiveInspection"},
	                   {"schemaVersion", 1},
	                   {"valid", true},
	                   {"requiredSymbols", ExtraSymbols},
	                   {"library", Path::PathToUtf8(fs::canonical(Archive))},
	                   {"libraryHash", Hash::HashFile(Archive)}})
	                 .dump(2)
	          << '\n';
	return std::cout ? 0 : 1;
}

int InspectionCommands::InspectBinary(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	for (const char* Required : {"--file", "--platform", "--architecture", "--kind"})
	{
		if (!Options.contains(Required))
		{
			throw std::runtime_error(std::string("inspect-binary requires ") + Required);
		}
	}
	const fs::path Binary = Path::PathFromUtf8(Options.at("--file"));
	BinaryInspector::ValidateHostBinary(Binary, Options.at("--platform"), Options.at("--architecture"),
	                                    Options.at("--kind"));
	std::cout << Json({{"format", "UMF.BinaryInspection"},
	                   {"schemaVersion", 1},
	                   {"valid", true},
	                   {"binary", Path::PathToUtf8(fs::canonical(Binary))},
	                   {"contentHash", Hash::HashFile(Binary)}})
	                 .dump(2)
	          << '\n';
	return std::cout ? 0 : 1;
}
} // namespace Mod::BuildTool
