// Copyright (c) 2026 Nelaric Contributors
/** @file ResponseFile.h */
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

class ResponseFile
{
public:
	/// Encodes tool arguments in a compiler-compatible response file.
	static void WriteResponseFile(const fs::path& Path, const std::vector<std::string>& Arguments,
	                              std::string_view Format);
};
} // namespace Mod::BuildTool
