// Copyright (c) 2026 Nelaric Contributors
#include "Workspace.h"

#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"

#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

Workspace::Workspace(const fs::path& Base)
{
	fs::create_directories(Base);
	Root = Base / Mod::BuildTool::Path::PathFromUtf8("umf-mst-" + Mod::BuildTool::TemporaryName::UniqueSuffix());
	if (!fs::create_directory(Root))
	{
		throw std::runtime_error("Cannot create an exclusive MST workspace.");
	}
}

Workspace::~Workspace()
{
	std::error_code Ignored;
	fs::remove_all(Root, Ignored);
}
} // namespace Mod::SdkTool
