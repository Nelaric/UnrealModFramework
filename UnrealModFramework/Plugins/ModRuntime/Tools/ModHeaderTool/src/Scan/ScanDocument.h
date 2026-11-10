// Copyright (c) 2026 Nelaric Contributors
/** @file ScanDocument.h */
#pragma once

#include "ModHeaderTool/src/Scan/ScanResult.h"

#include <filesystem>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class ScanDocument
{
public:
	static std::string DisplaySourcePath(const fs::path& Path, const fs::path& Root);
	static void WriteDocument(const std::string& Document, const fs::path& Output, const ScanResult& Result);
};
} // namespace Mod::HeaderTool
