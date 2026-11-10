// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataOptions.h */
#pragma once

#include "ModHeaderTool/src/Metadata/FunctionRedirect.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

/// Resolved choices supplied by the shared UE metadata provider.
struct MetadataOptions
{
	/// Canonical catalog file path.
	fs::path File;
	/// Case-sensitive choices keyed by their shared schema names.
	std::map<std::string, std::vector<std::string>, std::less<>> Choices;
	/// Effective UE function redirects from the configuration snapshot.
	std::vector<FunctionRedirect> FunctionRedirects;
	/// Actual configuration inputs, including explicit absent files.
	std::vector<fs::path> Inputs;
	/// Configuration directories monitored for additional INI files.
	std::vector<fs::path> ConfigRoots;
};
} // namespace Mod::HeaderTool
