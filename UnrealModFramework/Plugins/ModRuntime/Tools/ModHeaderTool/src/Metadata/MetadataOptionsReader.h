// Copyright (c) 2026 Nelaric Contributors
/** @file MetadataOptionsReader.h */
#pragma once

#include "ModHeaderTool/src/Metadata/MetadataOptions.h"
#include "ModHeaderTool/src/Project/Project.h"

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class MetadataOptionsReader
{
public:
	/// Loads an authoritative option catalog and rejects stale config inputs.
	static MetadataOptions LoadMetadataOptions(const Project& Project, const fs::path& File);
};
} // namespace Mod::HeaderTool
