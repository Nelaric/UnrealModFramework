// Copyright (c) 2026 Nelaric Contributors
/** @file Sdk.h */
#pragma once

#include "FileSystem.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using FJson = nlohmann::json;

/// Compares normalized paths, ignoring case on Windows.
bool SamePath(const fs::path& Left, const fs::path& Right);
/// Returns whether a normalized path is within the given directory.
bool IsInside(const fs::path& Path, const fs::path& Root);

/// One validated MBT library request and its frozen build plan.
struct FLibrary
{
	/// Whether this archive is private Host code or public Client code.
	std::string Role;
	/// Actual owning-module compilation context supplied by the producer.
	fs::path Context;
	/// Complete generator batch consumed by MBT.
	fs::path Manifest;
	/// MBT's checked input fingerprint and output identity.
	FJson Plan;
};

/// Validated SDK inputs; source semantics belong to their export producers.
struct FSdkRequest
{
	/// Canonical request file used to resolve relative paths.
	fs::path File;
	/// Original, validated build request.
	FJson Document;
	/// Frozen public contract and projection supplied by the generator.
	FJson Contract;
	/// Canonical public contract file.
	fs::path ContractFile;
	/// Game project used for identity and editor diagnostics.
	fs::path Project;
	/// Versioned bundle directory owned by MST.
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
	std::vector<FLibrary> Libraries;
	/// Public include paths mapped to validated generated source files.
	FJson Headers = FJson::array();
	/// Optional hash-checked public connection descriptor supplied by MHT.
	FJson ConnectionArtifact = nullptr;
	/// Optional declaration export used to generate the global Mod entry points.
	FJson LifecycleArtifact = nullptr;
	/// Fingerprint of the frozen SDK input set.
	std::string Fingerprint;
	/// Input files protected from result writes and mid-build changes.
	FJson Inputs = FJson::array();
};

/// Reads bounded JSON with path-qualified diagnostics.
FJson ReadJson(const fs::path& Path);
/// Reads a required nonempty JSON string without control characters.
std::string Text(const FJson& Document, const char* Key);
/// Requires a protocol format and supported schema version.
void RequireFormat(const FJson& Document, std::string_view Format);
/// Loads and validates a build request and its public contract.
FSdkRequest ReadSdkRequest(const fs::path& File);
/// Resolves real MBT plans and public headers without compiling libraries.
FJson PlanSdk(FSdkRequest& Request);
/// Generates adapters, builds native libraries and UE plugins, and publishes.
FJson BuildSdk(FSdkRequest& Request);

/// Native tools and author Editor identity for an Unreal plugin build.
struct FEditorBuild
{
	/// Actual engine-provided .NET host.
	fs::path Dotnet;
	/// Actual UE AutomationTool assembly.
	fs::path AutomationTool;
	/// Native MBT executable used to verify the built binary.
	fs::path Mbt;
	/// Unreal host platform for the Editor target.
	std::string Platform;
	/// Native architecture of the Editor binary.
	std::string Architecture;
	/// Build identity read from the author Editor module manifest.
	std::string BuildId;
	/// Maximum runtime for each native tool invocation.
	unsigned TimeoutSeconds = 1800;
};

/// Builds and publishes the independent authoring plugin.
void BuildEditorPlugin(const FEditorBuild& Editor, const fs::path& Plugin, const fs::path& Package);
/// Verifies an existing bundle's complete declared file inventory.
FJson InspectSdk(const fs::path& Directory);
/// Rejects a result path that aliases inputs, libraries, or bundle content.
void ValidateResultOutput(const FSdkRequest& Request, const fs::path& Output);
/// Generates UE SDK source from supported, already resolved projections.
void GenerateSdkPlugin(const FSdkRequest& Request, const fs::path& Directory);
/// Runs a native child with a deadline and process-tree cancellation.
int RunTool(const fs::path& Executable, const std::vector<std::string>& Arguments, const fs::path& WorkingDirectory,
            unsigned TimeoutSeconds);
/// Emits one machine-readable progress event to stderr.
void ReportStage(std::string_view Stage, std::string_view Message);
} // namespace Mod::SdkTool
