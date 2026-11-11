// Copyright (c) 2026 Nelaric Contributors
/** @file SdkRequest.h */
#pragma once

#include "ModSDKTool/src/Manifest/Library.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

/// Validated SDK inputs; source semantics belong to their export producers.
struct SdkRequest
{
	/// Canonical request file used to resolve relative paths.
	fs::path File;
	/// Original, validated build request.
	Json Document;
	/// Frozen public contract and projection supplied by the generator.
	Json Contract;
	/// Canonical public contract file.
	fs::path ContractFile;
	/// Game project used for identity and editor diagnostics.
	fs::path Project;
	/// Versioned bundle directory owned by MDT.
	fs::path Output;
	/// Native MBT executable, never a shell command.
	fs::path Mbt;
	/// Actual engine-provided .NET host.
	fs::path Dotnet;
	/// Actual UE AutomationTool assembly.
	fs::path AutomationTool;
	/// Independent development editor plugin template.
	fs::path DevelopmentPlugin;
	/// Checked per-module library jobs.
	std::vector<Library> Libraries;
	/// Public include paths mapped to validated generated source files.
	Json Headers = Json::array();
	/// Optional hash-checked public connection descriptor supplied by MHT.
	Json ConnectionArtifact = nullptr;
	/// Optional declaration export used to generate the global Mod entry points.
	Json LifecycleArtifact = nullptr;
	/// Fingerprint of the frozen SDK input set.
	std::string Fingerprint;
	/// Input files protected from result writes and mid-build changes.
	Json Inputs = Json::array();
};
} // namespace Mod::SdkTool
