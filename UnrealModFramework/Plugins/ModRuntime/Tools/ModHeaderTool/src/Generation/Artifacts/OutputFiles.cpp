// Copyright (c) 2026 Nelaric Contributors
#include "OutputFiles.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#endif

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

void OutputFiles::Write(const fs::path& File, const std::string& Bytes)
{
	fs::create_directories(File.parent_path());
	std::ofstream Stream(File, std::ios::binary | std::ios::trunc);
	Stream.write(Bytes.data(), static_cast<std::streamsize>(Bytes.size()));
	Stream.close();
	if (!Stream)
	{
		throw std::runtime_error("Cannot write generated output: " + DiagnosticLocation::QuoteDiagnosticPath(File));
	}
}

void OutputFiles::Replace(const fs::path& Source, const fs::path& Destination)
{
#ifdef _WIN32
	if (!MoveFileExW(Source.c_str(), Destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		throw std::runtime_error("Cannot replace generated output: " +
		                         DiagnosticLocation::QuoteDiagnosticPath(Destination));
	}
#else
	fs::rename(Source, Destination);
#endif
}
} // namespace Mod::HeaderTool
