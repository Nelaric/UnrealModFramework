// Copyright (c) 2026 Nelaric Contributors
/** @file HostContext.h */
#pragma once

#include <filesystem>
#include <string>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class HostContext
{
public:
	/// Hashes the parsed, sorted JSON context used by generated manifests.
	static std::string HashHostContext(const fs::path& Context);
};
} // namespace Mod::BuildTool
