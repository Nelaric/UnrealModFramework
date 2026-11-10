// Copyright (c) 2026 Nelaric Contributors
/** @file Workspace.h */
#pragma once

#include <filesystem>

namespace Mod::SdkTool
{
namespace fs = std::filesystem;

struct Workspace
{
	fs::path Root;
	explicit Workspace(const fs::path& Base);
	~Workspace();
};
} // namespace Mod::SdkTool
