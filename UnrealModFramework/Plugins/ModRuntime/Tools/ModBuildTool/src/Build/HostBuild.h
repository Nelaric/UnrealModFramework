// Copyright (c) 2026 Nelaric Contributors
/** @file HostBuild.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

/// Manages the archive for one producer context and generation batch.
class HostBuild
{
public:
	/// Associates the producer context with its generated artifacts.
	HostBuild(fs::path ContextFile, fs::path ManifestFile);

	/// Validates the current inputs without building.
	Json Plan() const;
	/// Builds or reuses the archive for the expected input fingerprint.
	Json Build(std::string_view ExpectedInput) const;
	/// Verifies the current archive against a trusted successful link record.
	Json Finalize(const fs::path& LinkReceipt) const;
	/// Checks whether a result destination would replace an input or artifact.
	void ValidateResultOutput(const fs::path& Output, const fs::path& LinkReceipt) const;

private:
	fs::path Context;
	fs::path Manifest;
};
} // namespace Mod::BuildTool
