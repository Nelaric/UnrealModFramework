// Copyright (c) 2026 Nelaric Contributors
/** @file Request.h */
#pragma once

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

struct Request
{
	fs::path ContextPath;
	fs::path ManifestPath;
	fs::path Root;
	fs::path WorkingDirectory;
	fs::path Library;
	fs::path Receipt;
	Json Context;
	Json Manifest;
	std::string ContextHash;
	std::string GenerationId;
	std::string NativeABIHash;
	std::string Owner;
	std::string Anchor;
	std::string Platform;
	std::string Architecture;
	std::vector<fs::path> Sources;
	std::vector<std::string> RequiredSymbols;
	Json RequiredBindings = Json::array();
	Json PublicArtifacts = Json::array();
	Json OwningBinary = nullptr;
	fs::path BinaryPath;
	bool bPeLinkWitness = false;
	std::map<fs::path, std::string> Inputs;
	std::map<std::string, std::string> Environment;
	std::string EnvironmentHash;
};
} // namespace Mod::BuildTool
