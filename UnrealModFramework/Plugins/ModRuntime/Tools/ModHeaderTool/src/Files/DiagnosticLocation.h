// Copyright (c) 2026 Nelaric Contributors
/** @file DiagnosticLocation.h */
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class DiagnosticLocation
{
public:
	/// Formats a filesystem path in double quotes for diagnostic output.
	static std::string QuoteDiagnosticPath(const fs::path& Path);
	/// Formats a diagnostic path followed by its one-based source coordinates.
	static std::string FormatDiagnosticLocation(const fs::path& Path, std::size_t Line, std::size_t Column);
};
} // namespace Mod::HeaderTool
