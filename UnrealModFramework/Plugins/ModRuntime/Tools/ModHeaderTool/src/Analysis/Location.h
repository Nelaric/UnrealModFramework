// Copyright (c) 2026 Nelaric Contributors
/** @file Location.h */
#pragma once

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

struct Location
{
	fs::path File;
	unsigned Offset = 0;
};
} // namespace Mod::HeaderTool
