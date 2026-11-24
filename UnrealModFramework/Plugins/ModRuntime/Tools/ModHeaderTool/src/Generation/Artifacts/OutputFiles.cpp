// Copyright (c) 2026 Nelaric Contributors
#include "OutputFiles.h"

#include "ModHeaderTool/src/Files/DiagnosticLocation.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

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
} // namespace Mod::HeaderTool
