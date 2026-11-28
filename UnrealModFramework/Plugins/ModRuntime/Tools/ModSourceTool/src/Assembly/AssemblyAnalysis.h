// Copyright (c) 2026 Nelaric Contributors
/** @file AssemblyAnalysis.h */
#pragma once

#include "ModSourceTool/src/Analysis/SourceRequest.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

class AssemblyAnalysis
{
public:
	/// Associates validity failures with their optimized compiler branches.
	static void AnalyzeAssembly(const SourceRequest& Request, const fs::path& Compiler, const fs::path& Directory,
	                            const std::vector<std::string>& Arguments, std::string_view TargetTriple,
	                            std::optional<std::string>& CachedListing, Json& Report);
};
} // namespace Mod::SourceTool
