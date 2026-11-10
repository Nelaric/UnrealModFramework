// Copyright (c) 2026 Nelaric Contributors
/** @file AssemblyFiles.h */
#pragma once

#include "Common/Files/Path.h"
#include "Common/Files/TemporaryName.h"

#include <filesystem>

namespace Mod::SourceTool
{
namespace fs = std::filesystem;

struct AssemblyFiles
{
	const fs::path Root = fs::temp_directory_path() / Mod::BuildTool::Path::PathFromUtf8(
	                                                      "umf-asm-" + Mod::BuildTool::TemporaryName::UniqueSuffix());
	const fs::path Response = Root / "compile.rsp";
	const fs::path Listing = Root / "source.asm";
	const fs::path Object = Root / "source.obj";
	const fs::path Debug = Root / "source.pdb";

	AssemblyFiles();

	~AssemblyFiles();
};
} // namespace Mod::SourceTool
