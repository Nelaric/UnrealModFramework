// Copyright (c) 2026 Nelaric Contributors
/** @file OutputFiles.h */
#pragma once

#include <filesystem>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class OutputFiles
{
public:
	static void Write(const fs::path& File, const std::string& Bytes);
};
} // namespace Mod::HeaderTool
