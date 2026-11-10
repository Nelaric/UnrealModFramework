// Copyright (c) 2026 Nelaric Contributors
#include "DiagnosticLocation.h"

#include "ModHeaderTool/src/Files/Path.h"

#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

std::string DiagnosticLocation::QuoteDiagnosticPath(const fs::path& Path)
{
	std::ostringstream Output;
	Output << std::quoted(Path::PathToUtf8(Path));
	return Output.str();
}

std::string DiagnosticLocation::FormatDiagnosticLocation(const fs::path& Path, std::size_t Line, std::size_t Column)
{
	return QuoteDiagnosticPath(Path) + "(" + std::to_string(Line) + "," + std::to_string(Column) + ")";
}
} // namespace Mod::HeaderTool
