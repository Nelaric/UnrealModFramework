// Copyright (c) 2026 Nelaric Contributors
/** @file GenerationModel.h */
#pragma once

#include "ModHeaderTool/src/Generation/API.h"
#include "ModHeaderTool/src/Generation/Extension.h"
#include "ModHeaderTool/src/Generation/GenerationInput.h"
#include "ModHeaderTool/src/Generation/ResolvedType.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct GenerationModel
{
	GenerationInput Input;
	fs::path LifecycleSource;
	std::vector<API> APIs;
	std::map<std::string, Extension> Extensions;
	std::map<std::string, ResolvedType> Types;
	std::set<fs::path> Dependencies;
	std::vector<std::string> Commands;
	std::string ClangVersion;
	std::vector<fs::path> ConfigRoots;
	std::map<std::string, std::map<std::string, std::string>> ContextMacros;
	fs::path SourceErrorCatalog;
	std::string SourceErrorCatalogHash;
	int ErrorCodeEncodingVersion = 1;
	std::map<std::string, std::uint32_t> SourceErrorMacros;
};
} // namespace Mod::HeaderTool
