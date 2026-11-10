// Copyright (c) 2026 Nelaric Contributors
/** @file Hash.h */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class Hash
{
public:
	/// Returns the lowercase SHA-256 digest of exact bytes.
	static std::string HashBytes(std::string_view Bytes);
	/// Returns the SHA-256 digest of a regular file without text conversion.
	static std::string HashFile(const fs::path& Path);
	/// Requires a canonical lowercase SHA-256 digest.
	static void RequireDigest(std::string_view Digest, std::string_view Field);
};
} // namespace Mod::BuildTool
