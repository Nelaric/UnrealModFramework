// Copyright (c) 2026 Nelaric Contributors
/** @file OwnedStage.h */
#pragma once

#include <filesystem>

namespace Mod::BuildTool
{
namespace fs = std::filesystem;

struct OwnedStage
{
	fs::path Path;
	fs::path Parent;
	~OwnedStage();
};
} // namespace Mod::BuildTool
