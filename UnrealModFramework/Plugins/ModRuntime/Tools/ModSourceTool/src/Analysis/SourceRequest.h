// Copyright (c) 2026 Nelaric Contributors
/** @file SourceRequest.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

struct SourceRequest
{
	fs::path Database;
	fs::path Library;
	fs::path ResourceDir;
	fs::path Source;
	fs::path ProjectRoot;
	std::string Target;
	std::string Module;
	std::string Function;
	std::string Signature;
	std::vector<std::string> AnalysisArguments;
	std::optional<Json> FailureReturn;
	bool bAnalyzeAssembly = false;
};
} // namespace Mod::SourceTool
