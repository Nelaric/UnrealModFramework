// Copyright (c) 2026 Nelaric Contributors
/** @file Generation.h */
#pragma once

#include <filesystem>

namespace Mod::HeaderTool
{
namespace fs = std::filesystem;

class Generation
{
public:
	/// Resolves active C++ APIs and publishes complete generated artifacts.
	static void Generate(const fs::path& RequestFile);
};
} // namespace Mod::HeaderTool
