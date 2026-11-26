// Copyright (c) 2026 Nelaric Contributors
/** @file ExportModules.h */
#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Mod::Tools
{
/// Module roots selected for export discovery beside a UE project.
struct ExportModules
{
	std::map<std::string, std::filesystem::path> Modules;
	std::vector<std::filesystem::path> PluginDescriptors;

	/// Sorted absolute rules paths for incremental discovery.
	std::set<std::string> RulesFiles() const;

	/// Reads <project>.modexport and discovers UE module rules under the selected Source trees.
	static ExportModules Discover(const std::filesystem::path& Project);
};
} // namespace Mod::Tools
