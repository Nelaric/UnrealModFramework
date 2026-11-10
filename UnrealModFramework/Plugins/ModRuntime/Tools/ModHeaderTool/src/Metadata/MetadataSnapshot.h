// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataSnapshot.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class MetadataSnapshot
{
public:
	static constexpr char RefreshHint[] = ". Run the UE ModExportOptions commandlet again.";
	static std::vector<fs::path> PathArray(const Json& Document, const char* Field);
	static void CheckInputs(const Json& Document);

private:
	static std::string InputPath(const fs::path& Path);
	static std::string FileFingerprint(const fs::path& Path);
};
} // namespace Mod::HeaderTool
