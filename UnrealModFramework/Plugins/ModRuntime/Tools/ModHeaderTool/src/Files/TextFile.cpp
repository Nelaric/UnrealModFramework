// Copyright (c) 2026 Nelaric Contributors
#include "TextFile.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

std::string TextFile::ReadTextFile(const fs::path& Path)
{
	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		throw std::runtime_error("Cannot open file: " + DiagnosticLocation::QuoteDiagnosticPath(Path));
	}
	std::string Contents((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
	if (File.bad())
	{
		throw std::runtime_error("Cannot read file: " + DiagnosticLocation::QuoteDiagnosticPath(Path));
	}
	return Contents;
}
} // namespace Mod::HeaderTool
