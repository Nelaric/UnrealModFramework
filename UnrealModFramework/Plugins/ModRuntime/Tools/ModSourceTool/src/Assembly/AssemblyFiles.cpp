// Copyright (c) 2026 Nelaric Contributors
#include "AssemblyFiles.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

AssemblyFiles::AssemblyFiles()
{
	if (!fs::create_directory(Root))
	{
		throw std::runtime_error("Cannot create an assembly workspace: " + Mod::BuildTool::Path::PathToUtf8(Root));
	}
}

AssemblyFiles::~AssemblyFiles()
{
	std::error_code Error;
	for (const auto& Path : {Response, Listing, Object, Debug, Root})
	{
		fs::remove(Path, Error);
	}
}
} // namespace Mod::SourceTool
