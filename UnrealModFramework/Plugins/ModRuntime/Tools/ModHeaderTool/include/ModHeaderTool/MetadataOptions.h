// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataOptions.h */
#pragma once

#include "ModHeaderTool/Project.h"

#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
/// Resolved choices supplied by the shared UE metadata provider.
struct FMetadataOptions
{
	/// Canonical catalog file path.
	fs::path File;
	/// Case-sensitive choices keyed by their shared schema names.
	std::map<std::string, std::vector<std::string>, std::less<>> Choices;
	/// Actual configuration inputs, including explicit absent files.
	std::vector<fs::path> Inputs;
	/// Configuration directories monitored for additional INI files.
	std::vector<fs::path> ConfigRoots;
};

/// Loads an authoritative option catalog and rejects stale config inputs.
FMetadataOptions LoadMetadataOptions(const FProject& Project, const fs::path& File);
} // namespace Mod::HeaderTool
