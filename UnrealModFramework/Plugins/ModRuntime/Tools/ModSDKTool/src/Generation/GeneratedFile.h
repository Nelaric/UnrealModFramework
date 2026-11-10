// Copyright (c) 2026 Nelaric Contributors
/** @file GeneratedFile.h */
#pragma once

#include <filesystem>
#include <string_view>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

class GeneratedFile
{
public:
	static void WriteSource(const fs::path& File, std::string_view TextValue);
};
} // namespace Mod::SdkTool
