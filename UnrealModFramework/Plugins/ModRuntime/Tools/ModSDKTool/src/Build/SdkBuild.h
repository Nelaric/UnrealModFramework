// Copyright (c) 2026 Nelaric Contributors
/** @file SdkBuild.h */
#pragma once

#include "ModSDKTool/src/Build/SdkRequest.h"

#include <filesystem>
#include <nlohmann/json.hpp>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

/// Owns one validated SDK build request and its resolved plan.
class SdkBuild
{
public:
	/// Loads the build request and its public contract.
	explicit SdkBuild(const fs::path& File);

	/// Resolves native plans and public headers without compiling libraries.
	Json Plan();
	/// Builds native libraries and plugins and publishes the SDK bundle.
	Json Build();
	/// Rejects a result path that aliases an input, library, or bundle file.
	void ValidateResultOutput(const fs::path& Output) const;

private:
	static SdkRequest ReadRequest(const fs::path& File);
	void ValidatePublishInputs() const;

	SdkRequest Request;
};
} // namespace Mod::SdkTool
