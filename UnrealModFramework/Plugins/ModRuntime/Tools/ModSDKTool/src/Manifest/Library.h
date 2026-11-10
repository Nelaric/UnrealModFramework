// Copyright (c) 2026 Nelaric Contributors
/** @file Library.h */
#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

/// One validated MBT library request and its frozen build plan.
struct Library
{
	/// Whether this archive is private Host code or public Client code.
	std::string Role;
	/// Actual owning-module compilation context supplied by the producer.
	fs::path Context;
	/// Complete generator batch consumed by MBT.
	fs::path Manifest;
	/// MBT's checked input fingerprint and output identity.
	Json Plan;
};
} // namespace Mod::SdkTool
