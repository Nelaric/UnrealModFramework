// Copyright (c) 2026 Nelaric Contributors
#include "InspectionCommands.h"

#include "Common/Files/Path.h"
#include "Common/Hash/Hash.h"
#include "ModBuildTool/src/Archive/ArchiveInspector.h"
#include "ModBuildTool/src/Binary/BinaryInspector.h"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

int InspectionCommands::InspectArchive(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
	const fs::path Archive = Path::PathFromUtf8(Options.at("--file"));
	const std::vector<std::string> RequiredSymbols{Options.at("--anchor")};
	ArchiveInspector::ValidateHostArchive(Archive, Options.at("--platform"), Options.at("--architecture"),
	                                      RequiredSymbols);
	std::cout << Json({{"format", "UMF.ArchiveInspection"},
	                   {"schemaVersion", 1},
	                   {"valid", true},
	                   {"requiredSymbols", RequiredSymbols},
	                   {"library", Path::PathToUtf8(fs::canonical(Archive))},
	                   {"libraryHash", Hash::HashFile(Archive)}})
	                 .dump(2)
	          << '\n';
	return std::cout ? 0 : 1;
}

int InspectionCommands::InspectBinary(const CommandArguments& Command)
{
	const auto& Options = Command.Options;
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
