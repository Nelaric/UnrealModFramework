// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataOptions.h */
#pragma once

#include "ModHeaderTool/Project.h"

#include <map>
#include <string>
#include <vector>

namespace Mod::HeaderTool
{
/// The UE-parsed parts of a Core Redirect function path.
struct FRedirectFunctionName
{
	/// Script package, empty for an unqualified name.
	std::string Package;
	/// Reflected class name, empty for an unqualified name.
	std::string Class;
	/// Reflected function name.
	std::string Function;
};

/// An effective UE FunctionRedirects entry and its location in configuration.
struct FFunctionRedirect
{
	/// Previous name.
	FRedirectFunctionName OldName;
	/// Replacement name with qualifiers inherited from the previous name.
	FRedirectFunctionName NewName;
	/// Source INI location used in diagnostics.
	std::string Location;
	/// Effective configuration value.
	std::string Entry;
	/// Whether the redirect uses substring or wildcard matching.
	bool bPattern = false;
	/// Whether the entry explicitly names a replacement.
	bool bHasNewName = false;
};

/// Resolved choices supplied by the shared UE metadata provider.
struct FMetadataOptions
{
	/// Canonical catalog file path.
	fs::path File;
	/// Case-sensitive choices keyed by their shared schema names.
	std::map<std::string, std::vector<std::string>, std::less<>> Choices;
	/// Effective UE function redirects from the configuration snapshot.
	std::vector<FFunctionRedirect> FunctionRedirects;
	/// Actual configuration inputs, including explicit absent files.
	std::vector<fs::path> Inputs;
	/// Configuration directories monitored for additional INI files.
	std::vector<fs::path> ConfigRoots;
};

/// Loads an authoritative option catalog and rejects stale config inputs.
FMetadataOptions LoadMetadataOptions(const FProject& Project, const fs::path& File);
} // namespace Mod::HeaderTool
